#include "flir_kernel_os.h"
#include "fpga.h"
#include "fvdk_internal.h"
#include "linux/spi/spi.h"
#include "linux/firmware.h"
#include <linux/platform_device.h>
#include <linux/ipu-v3.h>
#include <linux/dma-mapping.h>

#define ERROR_NO_INIT_OK        10001
#define ERROR_NO_CONFIG_DONE    10002
#define ERROR_NO_SETUP          10003
#define ERROR_NO_SPI            10004
#define FW_DIR 			"FLIR/"
#define FW_FILE 		"yildun.bin"
#define SPI_MIN 		64

#define MEASURE_TIMING		0

static const struct firmware *pFW;

PUCHAR getFPGAData(PFVD_DEV_INFO pDev, ULONG * size, char *pHeader)
{
	GENERIC_FPGA_T *pGen;
	BXAB_FPGA_T *pSpec;
	int retval = 0;

	char filename[] = FW_DIR FW_FILE;
	retval = request_firmware(&pFW, filename, &pDev->pLinuxDevice->dev);
	if (retval) {
		dev_err(&pDev->pLinuxDevice->dev, "Failed to get file %s\n", filename);
		return (NULL);
	}

	pr_debug("Got %d bytes of firmware from %s\n", pFW->size, filename);

	/* Read generic header */
	if (pFW->size < sizeof(GENERIC_FPGA_T)) {
		return (NULL);
	}
	pGen = (GENERIC_FPGA_T *) pFW->data;
	if (pGen->headerrev > GENERIC_REV) {
		return (NULL);
	}
	if (pGen->spec_size > 1024) {
		return (NULL);
	}

	/* Read specific part */
	if (pFW->size < (sizeof(GENERIC_FPGA_T) + pGen->spec_size)) {
		return (NULL);
	}
	pSpec = (BXAB_FPGA_T *) & pFW->data[sizeof(GENERIC_FPGA_T)];

	/* Set FW size */
	*size = pFW->size - sizeof(GENERIC_FPGA_T) - pGen->spec_size;

	memcpy(pHeader, pFW->data, sizeof(GENERIC_FPGA_T) + pGen->spec_size);
	return ((PUCHAR) & pFW->data[sizeof(GENERIC_FPGA_T) + pGen->spec_size]);
}

void freeFpgaData(void)
{
	if (pFW) {
		release_firmware(pFW);
		pFW = NULL;
	}
}

int CheckFPGA(PFVD_DEV_INFO pDev)
{
	int res = ERROR_SUCCESS;

	if (0 == pDev->pGetPinDone(pDev)) {
		if (0 == pDev->pGetPinStatus(pDev))
			res = -ERROR_NO_INIT_OK;
		else
			res = -ERROR_NO_CONFIG_DONE;
		dev_err(&pDev->pLinuxDevice->dev, "CheckFPGA: FPGA load failed (%d)\n", res);
	}

	return res;
}

struct spi_board_info chip = {
	.modalias = "yildunspi",
	.max_speed_hz = 50000000,
	.mode = SPI_MODE_0,
};

#define tms(x) (x.tv_sec*1000 + x.tv_usec/1000)

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
	ULONG *buf=0;
	dma_addr_t phy;
#ifdef MEASURE_TIMING
	struct timeval t[6];

	do_gettimeofday(&t[0]);
#endif

	// read file
	fpgaBin = getFPGAData(pDev, &isize, pDev->fpga);
	if (fpgaBin == NULL) {
		dev_err(&pDev->pLinuxDevice->dev, "LoadFPGA: Error reading fpgadata file\n");
		retval = -ERROR_IO_DEVICE;
		goto ERROR;
	}

#ifdef MEASURE_TIMING
	do_gettimeofday(&t[1]);
#endif

	// Allocate a buffer suitable for DMA
	osize = (isize + SPI_MIN) & ~(SPI_MIN-1);
	buf = dma_alloc_coherent(&pDev->pLinuxDevice->dev, osize, &phy, GFP_DMA | GFP_KERNEL);
	if (!buf) {
		dev_err(&pDev->pLinuxDevice->dev, "LoadFPGA: Error allocating buf\n");
		retval = -ENOMEM;
		goto ERROR;
	}

	// swap bit and byte order
	if (((GENERIC_FPGA_T *) (pDev->fpga))->LSBfirst) {
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

#ifdef MEASURE_TIMING
	do_gettimeofday(&t[2]);
#endif

	// Put FPGA in programming mode
	retval = pDev->pPutInProgrammingMode(pDev);
	if (retval == 0) {
		msleep(10);
		if (pDev->pPutInProgrammingMode(pDev) == 0) {
			dev_err(&pDev->pLinuxDevice->dev,
					"LoadFPGA: Failed to set FPGA in programming mode\n");
	        retval = -ERROR_NO_SETUP;
	        goto ERROR;
		}
	}

#ifdef MEASURE_TIMING
	do_gettimeofday(&t[3]);
#endif

	// Send FPGA code through SPI
	pspim = spi_busnum_to_master(pDev->iSpiBus);
	if (pspim == NULL) {
		dev_err(&pDev->pLinuxDevice->dev, "LoadFPGA: Failed to get SPI master\n");
		retval = -ERROR_NO_SPI;
		goto ERROR;
	}
	pspid = spi_new_device(pspim, &chip);
	if (pspid == NULL) {
		dev_err(&pDev->pLinuxDevice->dev, "LoadFPGA: Failed to set SPI device\n");
		retval = -ERROR_NO_SPI;
		goto ERROR;
	}

	pspid->bits_per_word = 32;
	retval = spi_setup(pspid);

	retval = spi_write(pspid, buf, osize / pDev->iSpiCountDivisor);

	device_unregister(&pspid->dev);
	put_device(&pspim->dev);

#ifdef MEASURE_TIMING
	do_gettimeofday(&t[4]);
#endif

	if (CheckFPGA(pDev) != -ERROR_SUCCESS) {
		retval = -1;
		goto ERROR;
	}

#ifdef MEASURE_TIMING
	do_gettimeofday(&t[5]);

	// Printing mesage here breaks startup timing for SB 0601 detectors
	pr_info("FPGA loaded in %ld ms (read %ld rotate %ld prep %ld SPI %ld check %ld)\r\n",
		tms(t[5]) - tms(t[0]),
		tms(t[1]) - tms(t[0]),
		tms(t[2]) - tms(t[1]),
		tms(t[3]) - tms(t[2]),
		tms(t[4]) - tms(t[3]),
		tms(t[5]) - tms(t[4]));
#endif
	retval = 0;
ERROR:
	if (buf)
		dma_free_coherent(&pDev->pLinuxDevice->dev, osize, buf, phy);
	freeFpgaData();
	return retval;
}
