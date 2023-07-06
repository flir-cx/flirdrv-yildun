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
#include <linux/miscdevice.h>

struct yildun_data {
	PFVD_DEV_INFO pDev;
	struct miscdevice miscdev;
	struct device *dev;
};

static long ioctl(struct file *filep, unsigned int cmd, unsigned long arg);

static int init(struct device *dev);
static void deinit(struct device *dev);

/**
 * Yildun_Init
 *
 *
 * @return
 */
static int init(struct device *dev)
{
	struct yildun_data *data = dev_get_drvdata(dev);
	int retval = -1;

	pr_info("Yildun Init\n");

	data->pDev->pLinuxDevice->dev.dma_mask = devm_kmalloc(dev, sizeof(*data->pDev->pLinuxDevice->dev.dma_mask), GFP_KERNEL);

	if (!data->pDev->pLinuxDevice->dev.dma_mask)
		return -ENOMEM;

	*data->pDev->pLinuxDevice->dev.dma_mask = DMA_BIT_MASK(32);
	data->pDev->pLinuxDevice->dev.coherent_dma_mask = DMA_BIT_MASK(32);

	retval = SetupMX6S(data->pDev);
	if (retval) {
		pr_err("Error initializing MX6S for Yildun\n");
		return retval;
	}

	if (!data->pDev->pSetupGpioAccess) {
		pr_err("Error creating Yildun class\n");
		goto OUT_CLASSCREATE;
	}

	if (!data->pDev->pSetupGpioAccess(data->pDev)) {
		pr_err("Error setting up GPIO\n");
		goto OUT_DEVICECREATE;
	}

	return 0;

OUT_DEVICECREATE:
	data->pDev->pCleanupGpio(data->pDev);
OUT_CLASSCREATE:
	data->pDev->pBSPFvdPowerDown(data->pDev);
	return retval;
}

/**
 * Yildun_Deinit
 *
 */
static void deinit(struct device *dev)
{
	struct yildun_data *data = dev_get_drvdata(dev);
	data->pDev->pCleanupGpio(data->pDev);
}

static const struct file_operations yildun_misc_fops = {
	.owner = THIS_MODULE,
	.unlocked_ioctl = ioctl,
	/* .open = yildun_open, */
	/* .mmap = yildun_mmap, */
};

static int yildun_probe(struct platform_device *pdev)
{
	int ret;
	struct device *dev = &pdev->dev;

	struct yildun_data *data = devm_kzalloc(dev, sizeof(struct yildun_data), GFP_KERNEL);
	if (!data)
		return -ENOMEM;

	platform_set_drvdata(pdev, data);

	// Allocate (and zero-initiate) our control structure.
	data->pDev = (PFVD_DEV_INFO) devm_kzalloc(&pdev->dev, sizeof(FVD_DEV_INFO), GFP_KERNEL);
	if (!data->pDev)
		return -ENOMEM;

	data->pDev->pLinuxDevice = pdev;
	data->pDev->dev = dev;

	data->dev = dev;
	data->miscdev.minor = MISC_DYNAMIC_MINOR;
	data->miscdev.name = devm_kasprintf(dev, GFP_KERNEL, "yildun");
	data->miscdev.fops = &yildun_misc_fops;
	data->miscdev.parent = dev;

	ret = misc_register(&data->miscdev);
	if (ret) {
		dev_err(dev, "Failed to register miscdev for FVDK driver\n");
		//goto ERROR_MISC_REGISTER;
		//TODO: Fail!!
	}

	return init(dev);
}

static int yildun_remove(struct platform_device *pdev)
{
	struct yildun_data *data = platform_get_drvdata(pdev);
	struct device *dev = &pdev->dev;

	misc_deregister(&data->miscdev);
	deinit(dev);
	return 0;
}

/* static const struct dev_pm_ops yildun_pm_ops = { */
/* 	.suspend_late = yildun_suspend, */
/* 	.resume_early = yildun_resume, */
/* }; */

static const struct of_device_id yildun_match_table[] = {
	{ .compatible = "flir,yildun", },
	{}
};

static struct platform_driver yildun_driver = {
	.probe = yildun_probe,
	.remove = yildun_remove,
	.driver = {
		.of_match_table	= yildun_match_table,
		.name = "yildun-misc-driver",
		.owner = THIS_MODULE,
		/* .pm = &yildun_pm_ops, */
	},
};


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
	struct yildun_data *data = container_of(filep->private_data, struct yildun_data, miscdev);
	int ret = 0;
	static int enabled;

	switch (cmd) {
	case IOCTL_YILDUN_ENABLE:
		pr_debug("IOCTL_YILDUN_ENABLE\n");
		if (!enabled) {
			data->pDev->pBSPFvdPowerUp(data->pDev);
			ret = LoadFPGA(data->pDev);
			if (ret)
				data->pDev->pBSPFvdPowerDown(data->pDev);
			else
				enabled = TRUE;
		}
		break;

	case IOCTL_YILDUN_DISABLE:
		pr_debug("IOCTL_YILDUN_DISABLE\n");
		if (enabled) {
			data->pDev->pBSPFvdPowerDown(data->pDev);
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

module_platform_driver(yildun_driver);

MODULE_LICENSE("GPL");
MODULE_DESCRIPTION("FLIR Yildun FPGA loader");
MODULE_AUTHOR("Peter Fitger");
