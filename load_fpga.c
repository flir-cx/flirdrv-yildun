// SPDX-License-Identifier: GPL-2.0-or-later
#include "flir_kernel_os.h"
#include "fpga.h"
#include "yildun_internal.h"
#include "linux/spi/spi.h"
#include "linux/firmware.h"
#include <linux/platform_device.h>
#include <linux/ipu-v3.h>
#include <linux/dma-mapping.h>
#include <linux/delay.h>

#define ERROR_NO_INIT_OK        10001
#define ERROR_NO_CONFIG_DONE    10002
#define ERROR_NO_SETUP          10003
#define ERROR_NO_SPI            10004
#define FW_DIR "FLIR/"
#define FW_FILE "yildun.bin"
#define SPI_MIN 64

static const struct firmware *pFW;

static inline void msleep_range(unsigned long min, unsigned long max)
{
	usleep_range(min * 1000, max * 1000);
}


PUCHAR get_fpga_data(PFVD_DEV_INFO pDev, ULONG *size, char *pHeader)
{
	GENERIC_FPGA_T *pGen;
	BXAB_FPGA_T *pSpec;
	int retval = 0;
	char filename[] = FW_DIR FW_FILE;

	retval = request_firmware(&pFW, filename, pDev->dev);
	if (retval) {
		dev_err(pDev->dev, "Failed to get file %s\n", filename);
		return NULL;
	}

	pr_debug("Got %d bytes of firmware from %s\n", pFW->size, filename);

	/* Read generic header */
	if (pFW->size < sizeof(GENERIC_FPGA_T))
		return NULL;

	pGen = (GENERIC_FPGA_T *) pFW->data;
	if (pGen->headerrev > GENERIC_REV)
		return NULL;

	if (pGen->spec_size > 1024)
		return NULL;

	/* Read specific part */
	if (pFW->size < (sizeof(GENERIC_FPGA_T) + pGen->spec_size))
		return NULL;

	pSpec = (BXAB_FPGA_T *) &pFW->data[sizeof(GENERIC_FPGA_T)];

	/* Set FW size */
	*size = pFW->size - sizeof(GENERIC_FPGA_T) - pGen->spec_size;

	memcpy(pHeader, pFW->data, sizeof(GENERIC_FPGA_T) + pGen->spec_size);
	return ((PUCHAR) &pFW->data[sizeof(GENERIC_FPGA_T) + pGen->spec_size]);
}

void free_fpga_data(PFVD_DEV_INFO pDev)
{
	if (pFW) {
		dev_dbg(pDev->dev, "Releasing firmware data\n");
		release_firmware(pFW);
		pFW = NULL;
	}
}

int CheckFPGA(PFVD_DEV_INFO pDev)
{
	int res = ERROR_SUCCESS;

	if (pDev->pGetPinDone(pDev) == 0) {
		if (pDev->pGetPinStatus(pDev) == 0)
			res = -ERROR_NO_INIT_OK;
		else
			res = -ERROR_NO_CONFIG_DONE;
		dev_err(pDev->dev, "%s: FPGA load failed (%d)\n", __func__, res);
	}

	return res;
}

struct spi_board_info chip = {
	.modalias = "yildunspi",
	.max_speed_hz = 50000000,
	.mode = SPI_MODE_0,
};



/**
 * LoadFPGA
 *
 * @param pDev
 *
 * @return 0 on success
 *      negative on error
 */
int LoadFPGA(PFVD_DEV_INFO pDev)
{
	int retval = 0;
	unsigned long isize, osize;
	unsigned char *fpgaBin;
	struct spi_master *pspim;
	struct spi_device *pspid;
	char fpgaheader[400];
	ULONG *buf = 0;
	dma_addr_t phy;
	// read file
	fpgaBin = get_fpga_data(pDev, &isize, fpgaheader);
	if (fpgaBin == NULL) {
		dev_err(pDev->dev, "%s: Error reading fpgadata file\n", __func__);
		retval = -ERROR_IO_DEVICE;
		goto ERROR;
	}

	// Allocate a buffer suitable for DMA
	osize = (isize + SPI_MIN) & ~(SPI_MIN-1);
	dev_dbg(pDev->dev, "To allocate coherent buffer of size %lu \n", osize);
	buf = dma_alloc_coherent(pDev->dev, osize, &phy, GFP_DMA | GFP_KERNEL);
	if (!buf) {
		dev_err(pDev->dev, "%s: Error allocating buf\n", __func__);
		retval = -ENOMEM;
		goto ERROR;
	}

	// swap bit and byte order
	if (((GENERIC_FPGA_T *) (fpgaheader))->LSBfirst) {
		ULONG *iptr = (ULONG *) fpgaBin;
		ULONG *optr = buf;
		int len = (isize + 3) / 4;
		static const char reverseNibble[16] = { 0x00, 0x08, 0x04, 0x0C,	// 0, 1, 2, 3
			0x02, 0x0A, 0x06, 0x0E,	// 4, 5, 6, 7
			0x01, 0x09, 0x05, 0x0D,	// 8, 9, A, B
			0x03, 0x0B, 0x07, 0x0F};// C, D, E, F

		while (len--) {
			ULONG tmp = *iptr++;
			*optr++ = reverseNibble[tmp >> 28] |
				(reverseNibble[(tmp >> 24) & 0x0F] << 4) |
				(reverseNibble[(tmp >> 20) & 0x0F] << 8) |
				(reverseNibble[(tmp >> 16) & 0x0F] << 12) |
				(reverseNibble[(tmp >> 12) & 0x0F] << 16) |
				(reverseNibble[(tmp >> 8) & 0x0F] << 20) |
				(reverseNibble[(tmp >> 4) & 0x0F] << 24) |
				(reverseNibble[tmp & 0x0F] << 28);
		}
	} else {
		ULONG *iptr = (ULONG *) fpgaBin;
		ULONG *optr = buf;
		int len = (isize + 3) / 4;

		while (len--) {
			ULONG tmp = *iptr++;
			*optr++ = (tmp >> 24) |
			    ((tmp >> 8) & 0xFF00) |
			    ((tmp << 8) & 0xFF0000) | (tmp << 24);
		}
	}

	// Put FPGA in programming mode
	retval = pDev->pPutInProgrammingMode(pDev);
	if (retval == 0) {
		msleep_range(10, 20);
		if (pDev->pPutInProgrammingMode(pDev) == 0) {
			dev_err(pDev->dev, "%s: Failed to set FPGA in programming mode\n", __func__);
			retval = -ERROR_NO_SETUP;
			goto ERROR;
		}
	}

	// Send FPGA code through SPI
	pspim = spi_busnum_to_master(pDev->iSpiBus);
	if (pspim == NULL) {
		dev_err(pDev->dev, "%s: Failed to get SPI master\n", __func__);
		retval = -ERROR_NO_SPI;
		goto ERROR;
	}
	pspid = spi_new_device(pspim, &chip);
	if (pspid == NULL) {
		dev_err(pDev->dev, "%s: Failed to set SPI device\n", __func__);
		retval = -ERROR_NO_SPI;
		goto ERROR;
	}

	pspid->bits_per_word = 32;
	retval = spi_setup(pspid);

	retval = spi_write(pspid, buf, osize / pDev->iSpiCountDivisor);

	device_unregister(&pspid->dev);
	put_device(&pspim->dev);

	if (CheckFPGA(pDev) != -ERROR_SUCCESS) {
		retval = -1;
		goto ERROR;
	}

	retval = 0;
ERROR:
	dev_dbg(pDev->dev, "Releasing  coherent buffer\n");
	dma_free_coherent(pDev->dev, osize, buf, phy);
	free_fpga_data(pDev);
	return retval;
}
