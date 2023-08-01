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
#define DMA_CHUNK_SIZE PAGE_SIZE // At least 64 bytes for the SPI

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

	dev_dbg(pDev->dev, "Got %d bytes of firmware from %s\n", pFW->size, filename);

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
	if (pDev->pGetPinDone(pDev))
		return 0;

	if (pDev->pGetPinStatus(pDev))
		return -ERROR_NO_CONFIG_DONE;

	return -ERROR_NO_INIT_OK;
}

struct spi_board_info chip = {
	.modalias = "yildunspi",
	.max_speed_hz = 50000000,
	.mode = SPI_MODE_0,
};

static inline u32 reverse_bits(u32 data)
{
#ifdef __arm__
	u32 src = data;
	u32 dst;

	asm ("rbit %0, %1\n"
	     : "=r" (dst)
	     : "r" (src));

	return dst;
#else
	static const char rnibble[16] = { 0x00, 0x08, 0x04, 0x0C, // 0, 1, 2, 3
		0x02, 0x0A, 0x06, 0x0E,	// 4, 5, 6, 7
		0x01, 0x09, 0x05, 0x0D,	// 8, 9, A, B
		0x03, 0x0B, 0x07, 0x0F};// C, D, E, F
	u32 result = rnibble[tmp >> 28] |
		(rnibble[(tmp >> 24) & 0x0F] << 4) |
		(rnibble[(tmp >> 20) & 0x0F] << 8) |
		(rnibble[(tmp >> 16) & 0x0F] << 12) |
		(rnibble[(tmp >> 12) & 0x0F] << 16) |
		(rnibble[(tmp >> 8) & 0x0F] << 20) |
		(rnibble[(tmp >> 4) & 0x0F] << 24) |
		(rnibble[tmp & 0x0F] << 28);

	return result;
#endif
}

static void fill_dma_buf(unsigned long *iptr, unsigned long *optr, unsigned long len, bool lsb_first)
{
	// swap bit and byte order
	if (lsb_first) {
		while (len--)
			*optr++ = reverse_bits(*iptr++);
	} else {
		while (len--) {
			unsigned long tmp = *iptr++;
			*optr++ = (tmp >> 24) |
			    ((tmp >> 8) & 0xFF00) |
			    ((tmp << 8) & 0xFF0000) | (tmp << 24);
		}
	}
}

static int spi_configure(PFVD_DEV_INFO pDev, struct spi_master **master, struct spi_device **device)
{
	*master = spi_busnum_to_master(pDev->iSpiBus);
	if (*master == NULL) {
		dev_err(pDev->dev, "%s: Failed to get SPI master\n", __func__);
		return -ERROR_NO_SPI;
	}
	*device = spi_new_device(*master, &chip);
	if (*device == NULL) {
		dev_err(pDev->dev, "%s: Failed to set SPI device\n", __func__);
		return -ERROR_NO_SPI;
	}

	(*device)->bits_per_word = 32;
	return spi_setup(*device);
}

static void spi_release(struct spi_master *master, struct spi_device *device)
{
	device_unregister(&device->dev);
	put_device(&master->dev);
}

static int fpga_set_programming_mode(PFVD_DEV_INFO pDev)
{
	int retval;

	retval = CheckFPGA(pDev);
	if (retval != -ERROR_NO_INIT_OK)
		dev_err(pDev->dev, "FPGA In unexpected state prior to programming mode (%i)\n", retval);

	retval = pDev->pPutInProgrammingMode(pDev);
	if (retval == 0) {
		msleep_range(10, 20);
		if (pDev->pPutInProgrammingMode(pDev) == 0) {
			dev_err(pDev->dev, "%s: Failed to set FPGA in programming mode\n", __func__);
			return -ERROR_NO_SETUP;
		}
	}

	retval = CheckFPGA(pDev);
	if (retval != -ERROR_NO_CONFIG_DONE)
		dev_err(pDev->dev, "FPGA In unexpected state after set to programming mode (%i)\n", retval);

	return 0;
}

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
	unsigned long isize, chunks, tailbytes;
	unsigned char *fpgaBin;
	unsigned long *iptr;
	struct spi_master *pspim;
	struct spi_device *pspid;
	char fpgaheader[400];
	bool lsb_first;
	ULONG *buf = 0;
	dma_addr_t phy;

	// read file
	fpgaBin = get_fpga_data(pDev, &isize, fpgaheader);
	if (fpgaBin == NULL) {
		dev_err(pDev->dev, "%s: Error reading fpgadata file\n", __func__);
		retval = -ERROR_IO_DEVICE;
		goto ERROR;
	}
	lsb_first = ((GENERIC_FPGA_T *)(fpgaheader))->LSBfirst;
	iptr = (unsigned long *)fpgaBin;

	buf = dma_alloc_coherent(pDev->dev, DMA_CHUNK_SIZE, &phy, GFP_DMA | GFP_KERNEL);
	if (!buf) {
		retval = -ENOMEM;
		goto ERROR;
	}

	retval = fpga_set_programming_mode(pDev);
	if (retval)
		goto ERROR;

	retval = spi_configure(pDev, &pspim, &pspid);
	if (retval)
		goto ERROR;

	chunks = isize / DMA_CHUNK_SIZE;
	tailbytes = isize - DMA_CHUNK_SIZE * chunks;
	if (tailbytes)
		chunks++;
	dev_dbg(pDev->dev, "Upload %lu chunks, with %lu trailing bytes\n", chunks, tailbytes);

	while (chunks--) {
		unsigned long len = DMA_CHUNK_SIZE / 4;

		if (!chunks && tailbytes)
			len = tailbytes / 4;
		fill_dma_buf(iptr, buf, len, lsb_first);
		retval = spi_write(pspid, buf, len * 4 / pDev->iSpiCountDivisor);
		iptr += len;
	}

	spi_release(pspim, pspid);

	if (CheckFPGA(pDev) != -ERROR_SUCCESS) {
		retval = -1;
		dev_err(pDev->dev, "FPGA Load failed\n");
		goto ERROR;
	}
	dev_dbg(pDev->dev, "FPGA Load ok\n");

	retval = 0;
ERROR:
	dev_dbg(pDev->dev, "Releasing coherent buffer\n");
	dma_free_coherent(pDev->dev, DMA_CHUNK_SIZE, buf, phy);
	free_fpga_data(pDev);
	return retval;
}
