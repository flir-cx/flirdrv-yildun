/***********************************************************************
 *                                                                     
 *    FLIR Yildun Device driver.
 *
 * Last check-in changelist:
 *
 * Copyright: FLIR Systems AB.  All rights reserved.
 *
 ***********************************************************************/

#ifndef __FVD_INTERNAL_H__
#define __FVD_INTERNAL_H__

#include <linux/proc_fs.h>

#define FVD_MINOR_VERSION   0
#define FVD_MAJOR_VERSION   1
#define FVD_VERSION ((FVD_MAJOR_VERSION << 16) | FVD_MINOR_VERSION)

// this structure keeps track of the device instance
typedef struct __FVD_DEV_INFO {
	// Linux driver variables
	struct device *dev;

	/* // FPGA header */
	/* char fpga[400];		// FPGA Header data buffer */

	// CPU specific function pointers
	BOOL(*pSetupGpioAccess) (struct __FVD_DEV_INFO * pDev);
	void (*pCleanupGpio) (struct __FVD_DEV_INFO * pDev);
	BOOL(*pGetPinDone) (struct __FVD_DEV_INFO * pDev);
	BOOL(*pGetPinStatus) (struct __FVD_DEV_INFO * pDev);
	BOOL(*pGetPinReady) (void);
	DWORD(*pPutInProgrammingMode) (struct __FVD_DEV_INFO * pDev);
	void (*pBSPFvdPowerUp) (struct __FVD_DEV_INFO * pDev);
	void (*pBSPFvdPowerDown) (struct __FVD_DEV_INFO * pDev);

	// CPU specific parameters
	int iSpiBus;
	int iSpiCountDivisor;

	// Pins
	int fpga_ce;
	int fpga_conf_done;
	int fpga_config;
	int fpga_status;
	int spi_sclk_gpio;
	int spi_mosi_gpio;

	// Regulators
	struct regulator *reg_1v1_fpga;
	struct regulator *reg_1v2_fpga;
	struct regulator *reg_1v8_fpga;
	struct regulator *reg_2v5_fpga;
	struct regulator *reg_3v15_fpga;

	// Pinmux
	struct pinctrl          *pinctrl;
	struct pinctrl_state    *pins_default;
	struct pinctrl_state    *pins_sleep;

} FVD_DEV_INFO, *PFVD_DEV_INFO;

#endif				/* __FVD_INTERNAL_H__ */
