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

static long ioctl(struct file *filep, unsigned int cmd, unsigned long arg);
static int open(struct inode *inode, struct file *filp);

static PFVD_DEV_INFO pDev;
static int __init init(void);
static void deinit(void);

static const struct file_operations yildun_fops = {
	.owner = THIS_MODULE,
	.unlocked_ioctl = ioctl,
	.open = open,
};

/**
 * Yildun_Init
 *
 *
 * @return
 */
static int __init init(void)
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

	pDev->dev = device_create(pDev->fvd_class, NULL, pDev->yildun_dev, NULL, "yildun");

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
static void deinit(void)
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
static int open(struct inode *inode, struct file *filp)
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
static long ioctl(struct file *filep, unsigned int cmd, unsigned long arg)
{
	int ret = 0;
	static int enabled;

	switch (cmd) {
	case IOCTL_YILDUN_ENABLE:
		pr_debug("IOCTL_YILDUN_ENABLE\n");
		if (!enabled) {
			pDev->pBSPFvdPowerUp(pDev);
			ret = LoadFPGA(pDev);
			if (ret)
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
		ret = -ERROR_NOT_SUPPORTED;
		break;
	}

	if (ret) {
		pr_err("Yildun Ioctl %X failed: %d\n", cmd, ret);
		goto OUT;
	}

OUT:
	return ret;
}

module_init(init);
module_exit(deinit);

MODULE_LICENSE("GPL");
MODULE_DESCRIPTION("FLIR Yildun FPGA loader");
MODULE_AUTHOR("Peter Fitger");
