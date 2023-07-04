// SPDX-License-Identifier: GPL-2.0-or-later
/***********************************************************************
 *
 * Project: Balthazar
 *
 * Description of file:
 *	Yildun FPGA control file
 *
 * Last check-in changelist:
 *
 * Copyright: FLIR Systems AB.  All rights reserved.
 *
 ***********************************************************************/

#include "yildun.h"
#include <linux/platform_device.h>
#include <linux/of.h>
#include <yildundev.h>
#include <linux/dma-mapping.h>

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

static const struct file_operations yildun_fops = {
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
	if (!pDev)
		return -ENOMEM;

	// Register linux driver
	i = alloc_chrdev_region(&pDev->yildun_dev, 0, 1, "yildun");

	if (i) {
		pr_err("Error allocating chrdev region\n");
		retval = -3;
		goto OUT_NOCHRDEV;
	}

	cdev_init(&pDev->yildun_cdev, &yildun_fops);
	pDev->yildun_cdev.owner = THIS_MODULE;
	pDev->yildun_cdev.ops = &yildun_fops;
	i = cdev_add(&pDev->yildun_cdev, pDev->yildun_dev, 1);
	if (i) {
		pr_err("Error adding device driver\n");
		retval = -3;
		goto OUT_NODEV;
	}
	pDev->pLinuxDevice = platform_device_alloc("yildun", 1);
	if (pDev->pLinuxDevice == NULL) {
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

	pDev->pLinuxDevice->dev.dma_mask = kmalloc(sizeof(*pDev->pLinuxDevice->dev.dma_mask), GFP_KERNEL);
	if (pDev->pLinuxDevice->dev.dma_mask == NULL) {
		retval = -ENOMEM;
		goto OUT_NODMAMASK;
	}
	*pDev->pLinuxDevice->dev.dma_mask = DMA_BIT_MASK(32);
	pDev->pLinuxDevice->dev.coherent_dma_mask = DMA_BIT_MASK(32);

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

	return 0;

OUT_DEVICECREATE:
	class_destroy(pDev->fvd_class);
	pDev->pCleanupGpio(pDev);
OUT_CLASSCREATE:
OUT_SETUPMX6Q:
	kfree(pDev->pLinuxDevice->dev.dma_mask);
OUT_NODMAMASK:
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
	return retval;
}

/**
 * Yildun_Deinit
 *
 */
static void Yildun_Deinit(void)
{
	device_destroy(pDev->fvd_class, pDev->yildun_dev);
	class_destroy(pDev->fvd_class);
	platform_device_unregister(pDev->pLinuxDevice);
	cdev_del(&pDev->yildun_cdev);
	unregister_chrdev_region(pDev->yildun_dev, 1);
	pDev->pCleanupGpio(pDev);
	kfree(pDev);
	pDev = NULL;
}

/**
 *  FVD_Open
 *
 * @param inode
 * @param filp
 *
 * @return
 */
static int Yildun_Open(struct inode *inode, struct file *filp)
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
	if (!tmp)
		return -ENOMEM;

	if (_IOC_DIR(cmd) & _IOC_WRITE) {
		dwErr = copy_from_user(tmp, (void *)arg, _IOC_SIZE(cmd));
		if (dwErr)
			pr_err("Yildun: Copy from user failed: %ld\n", dwErr);
	}

	if (dwErr == ERROR_SUCCESS) {
		dwErr = DoIOControl(pDev, cmd, tmp, (PUCHAR)arg);
		if (dwErr)
			pr_err("Yildun Ioctl %X failed: %ld\n", cmd, dwErr);
	}

	if ((dwErr == ERROR_SUCCESS) && (_IOC_DIR(cmd) & _IOC_READ)) {
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
	DWORD  dwErr = ERROR_SUCCESS;
	static int enabled;

	switch (Ioctl) {
	case IOCTL_YILDUN_ENABLE:
		pr_debug("IOCTL_YILDUN_ENABLE\n");
		if (!enabled) {
			pDev->pBSPFvdPowerUp(pDev);
			dwErr = LoadFPGA(pDev);
			if (dwErr)
				pDev->pBSPFvdPowerDown(pDev);
			else
				enabled = TRUE;
		}
		break;

	case IOCTL_YILDUN_DISABLE:
		pr_debug("IOCTL_YILDUN_DISABLE\n");
		if (enabled) {
			pDev->pBSPFvdPowerDown(pDev);
			enabled = FALSE;
		}
		break;

	default:
		dwErr = -ERROR_NOT_SUPPORTED;
		break;
	}
	return dwErr;
}

module_init(Yildun_Init);
module_exit(Yildun_Deinit);

MODULE_LICENSE("GPL");
MODULE_DESCRIPTION("FLIR Yildun FPGA loader");
MODULE_AUTHOR("Peter Fitger");
