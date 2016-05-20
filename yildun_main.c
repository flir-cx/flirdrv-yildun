/***********************************************************************
 *
 * Project: Balthazar
 * $Date$
 * $Author$
 *
 * $Id$
 *
 * Description of file:
 *	Yildun FPGA control file
 *
 * Last check-in changelist:
 * $Change$
 *
 * Copyright: FLIR Systems AB.  All rights reserved.
 *
 ***********************************************************************/

#include "yildun.h"
#include <linux/platform_device.h>
#include <linux/of.h>

static long Yildun_IOControl(struct file *filep,
						  unsigned int cmd,
						  unsigned long arg);
static int Yildun_Open(struct inode *inode,
					struct file *filp);
static DWORD DoIOControl(PFVD_DEV_INFO pDev,
						 DWORD  Ioctl,
						 PUCHAR pBuf,
						 PUCHAR pUserBuf);

static PFVD_DEV_INFO pDev;
static int __init Yildun_Init(void);
static void Yildun_Deinit(void);
static int Yildun_LoadFirmware(void);
static int Yildun_powerdown(struct platform_device *dev,
			  int enable);

static struct file_operations yildun_fops =
{
	.owner = THIS_MODULE,
	.unlocked_ioctl = Yildun_IOControl,
	.open = Yildun_Open,
};

/**
 * Yildun_Init
 *
 *
 * @return
 */
static int __init Yildun_Init(void)
{
	int retval = -1;
	int i;
	pr_info("Yildun Init\n");

	// Allocate (and zero-initiate) our control structure.
	pDev = (PFVD_DEV_INFO) kzalloc(sizeof(FVD_DEV_INFO), GFP_KERNEL);
	if (pDev == NULL) {
		pr_err("Error allocating memory for pDev, Yildun_Init failed\n");
		goto OUT_NOMEM;
	}
	// Register linux driver
	i = alloc_chrdev_region(&pDev->yildun_dev, 0, 1, "yildun");

	if (i){
		pr_err("Error allocating chrdev region\n");
		retval = -3;
		goto OUT_NOCHRDEV;
	}

	cdev_init(&pDev->yildun_cdev, &yildun_fops);
	pDev->yildun_cdev.owner = THIS_MODULE;
	pDev->yildun_cdev.ops = &yildun_fops;
	i = cdev_add(&pDev->yildun_cdev, pDev->yildun_dev, 1);
	if (i){
		pr_err("Error adding device driver\n");
		retval = -3;
		goto OUT_NODEV;
	}
	pDev->pLinuxDevice = platform_device_alloc("yildun", 1);
	if (pDev->pLinuxDevice == NULL){
		pr_err("Error adding allocating device\n");
		retval = -4;
		goto OUT_NODEVALLOC;
	}

	pDev->pLinuxDevice = platform_device_alloc("yildun", 1);
	if (pDev->pLinuxDevice == NULL) {
		pr_err("Error adding allocating device\n");
		goto OUT_PLATFORMDEVICEALLOC;
	}
	retval = platform_device_add(pDev->pLinuxDevice);
	if (retval) {
		pr_err("Error adding platform device\n");
		goto OUT_PLATFORMDEVICEADD;
	}

	platform_set_drvdata(pDev->pLinuxDevice, pDev);

	retval = SetupMX6S(pDev);
	if (retval) {
		pr_err("Error initializing MX6S for Yildun\n");
		goto OUT_SETUPMX6Q;
	}

	pDev->fvd_class = class_create(THIS_MODULE, "yildun");
	if (!pDev->pSetupGpioAccess) {
		pr_err("Error creating Yildun class\n");
		goto OUT_CLASSCREATE;
	}

	pDev->dev = device_create(pDev->fvd_class, 
				  NULL, 
				  pDev->yildun_dev,
				  NULL, 
				  "yildun");

	if (!pDev->pSetupGpioAccess(pDev)) {
		pr_err("Error setting up GPIO\n");
		goto OUT_DEVICECREATE;
	}

	sema_init(&pDev->muDevice, 1);
	pDev->pBSPFvdPowerUp(pDev);
	retval = Yildun_LoadFirmware();
	if (retval) {
		pr_err("Yildun_Loadfirmware failure\n");
		goto OUT_LOADFIRMWARE;
	}
	pr_info("Yildun Firmware loaded\n");

	return retval;

OUT_LOADFIRMWARE:
	device_destroy(pDev->fvd_class, pDev->yildun_dev);
OUT_DEVICECREATE:
	class_destroy(pDev->fvd_class);
	pDev->pCleanupGpio(pDev);
OUT_CLASSCREATE:
OUT_SETUPMX6Q:
	platform_device_del(pDev->pLinuxDevice);
OUT_PLATFORMDEVICEADD:
	platform_device_put(pDev->pLinuxDevice);
OUT_PLATFORMDEVICEALLOC:
	pDev->pBSPFvdPowerDown(pDev);
OUT_NODEVALLOC:
	cdev_del(&pDev->yildun_cdev);
OUT_NODEV:
	unregister_chrdev_region(pDev->yildun_dev, 1);
OUT_NOCHRDEV:
	kfree(pDev);
	pDev = NULL;
OUT_NOMEM:
	return retval;
}

/**
 * Yildun_Deinit
 *
 */
static void Yildun_Deinit(void)
{
	pr_debug("Yildun_Deinit\n");
	Yildun_powerdown(pDev->pLinuxDevice, 1);
	device_destroy(pDev->fvd_class, pDev->yildun_dev);
	class_destroy(pDev->fvd_class);
	platform_device_unregister(pDev->pLinuxDevice);
	pDev->pBSPFvdPowerDown(pDev);
	cdev_del(&pDev->yildun_cdev);
	unregister_chrdev_region(pDev->yildun_dev, 1);
	pDev->pCleanupGpio(pDev);
	kfree(pDev);
	pDev = NULL;
}

/** 
 * Load firmware for Yildun Device, check for errors during
 * spi transfer, check that FPGA becomes loaded and ready
 * 
 * @return 0 on success
 * @return negative on error
 */
static int Yildun_LoadFirmware(void)
{
	int retval;

	down(&pDev->muDevice);
	retval = LoadFPGA(pDev);
	up(&pDev->muDevice);
	return retval;
}

/** 
 * enable/disable the viewfinder
 * 
 * @param enable 
 * 
 * @return 
 */
static int Yildun_powerdown(struct platform_device *dev, int enable)
{
	PFVD_DEV_INFO pDev;
	pDev = platform_get_drvdata(dev);

	if (enable) {
//		gpio_set_value(pDev->pwdn, 1);
	} else {
//		gpio_set_value(pDev->pwdn, 0);
	}
	return 0;
}

/**
 *  FVD_Open
 *
 * @param inode
 * @param filp
 *
 * @return
 */
static int Yildun_Open (struct inode *inode, struct file *filp)
{
	return 0;
}

/** 
 * Yildun_IOControl
 * 
 * @param filep 
 * @param cmd 
 * @param arg 
 * 
 * @return 
 */
static long Yildun_IOControl(struct file *filep,
		unsigned int cmd, unsigned long arg)
{
	DWORD dwErr = ERROR_SUCCESS;
	char *tmp;

	tmp = kzalloc(_IOC_SIZE(cmd), GFP_KERNEL);
	if (_IOC_DIR(cmd) & _IOC_WRITE)
	{
		dwErr = copy_from_user(tmp, (void *)arg, _IOC_SIZE(cmd));
		if (dwErr)
			pr_err("Yildun: Copy from user failed: %ld\n", dwErr);
	}

	if (dwErr == ERROR_SUCCESS)
	{
		dwErr = DoIOControl(pDev, cmd, tmp, (PUCHAR)arg);
		if (dwErr)
			pr_err("Yildun Ioctl %X failed: %ld\n", cmd, dwErr);
	}

	if ((dwErr == ERROR_SUCCESS) && (_IOC_DIR(cmd) & _IOC_READ))
	{
		dwErr = copy_to_user((void *)arg, tmp, _IOC_SIZE(cmd));
		if (dwErr)
			pr_err("Yildun: Copy to user failed: %ld\n", dwErr);
	}
	kfree(tmp);
	return dwErr;
}

/** 
 * DoIOControl
 * 
 * @param pDev 
 * @param Ioctl 
 * @param pBuf 
 * @param pUserBuf 
 * 
 * @return 
 */
DWORD DoIOControl(PFVD_DEV_INFO pDev,
		  DWORD  Ioctl,
		  PUCHAR pBuf,
		  PUCHAR pUserBuf)
{
	DWORD  dwErr = ERROR_INVALID_PARAMETER;

	switch (Ioctl)
	{
	default:
		dwErr = ERROR_NOT_SUPPORTED;
		break;
	}
	return dwErr;
}

module_init(Yildun_Init);
module_exit(Yildun_Deinit);

MODULE_LICENSE("GPL");
MODULE_DESCRIPTION("FLIR Yildun FPGA loader");
MODULE_AUTHOR("Peter Fitger");
