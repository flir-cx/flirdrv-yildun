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

static PFVD_DEV_INFO pDev;
static int __init init(void);
static void deinit(void);

/**
 * Yildun_Init
 *
 *
 * @return
 */
static int __init init(void)
{
	int retval = -1;

	pr_info("Yildun Init\n");

	pDev->pLinuxDevice->dev.dma_mask = devm_kmalloc(pDev->dev, sizeof(*pDev->pLinuxDevice->dev.dma_mask), GFP_KERNEL);

	if (!pDev->pLinuxDevice->dev.dma_mask)
		return -ENOMEM;

	*pDev->pLinuxDevice->dev.dma_mask = DMA_BIT_MASK(32);
	pDev->pLinuxDevice->dev.coherent_dma_mask = DMA_BIT_MASK(32);

	retval = SetupMX6S(pDev);
	if (retval) {
		pr_err("Error initializing MX6S for Yildun\n");
		return retval;
	}

	if (!pDev->pSetupGpioAccess) {
		pr_err("Error creating Yildun class\n");
		goto OUT_CLASSCREATE;
	}

	if (!pDev->pSetupGpioAccess(pDev)) {
		pr_err("Error setting up GPIO\n");
		goto OUT_DEVICECREATE;
	}

	return 0;

OUT_DEVICECREATE:
	pDev->pCleanupGpio(pDev);
OUT_CLASSCREATE:
	pDev->pBSPFvdPowerDown(pDev);
	return retval;
}

/**
 * Yildun_Deinit
 *
 */
static void deinit(void)
{
	pDev->pCleanupGpio(pDev);
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
	struct yildun_data *data = devm_kzalloc(&pdev->dev, sizeof(struct yildun_data), GFP_KERNEL);
	if (!data)
		return -ENOMEM;

	platform_set_drvdata(pdev, data);
	/* data->pDev = (PFVD_DEV_INFO) devm_kzalloc(&pdev->dev, sizeof(FVD_DEV_INFO), GFP_KERNEL); */
	/* if (!data->pDev) */
	/* 	return -ENOMEM; */

	data->dev = &pdev->dev;
	data->miscdev.minor = MISC_DYNAMIC_MINOR;
	data->miscdev.name = devm_kasprintf(&pdev->dev, GFP_KERNEL, "yildun");
	data->miscdev.fops = &yildun_misc_fops;
	data->miscdev.parent = &pdev->dev;

	ret = misc_register(&data->miscdev);
	if (ret) {
		dev_err(&pdev->dev, "Failed to register miscdev for FVDK driver\n");
		//goto ERROR_MISC_REGISTER;
	}

	// Allocate (and zero-initiate) our control structure.
	pDev = (PFVD_DEV_INFO) devm_kzalloc(&pdev->dev, sizeof(FVD_DEV_INFO), GFP_KERNEL);
	if (!pDev)
		return -ENOMEM;

	pDev->pLinuxDevice = pdev;
	pDev->dev = &pdev->dev;
	return init();
}

static int yildun_remove(struct platform_device *pdev)
{
	struct yildun_data *data = platform_get_drvdata(pdev);

	misc_deregister(&data->miscdev);
	deinit();
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

module_platform_driver(yildun_driver);

MODULE_LICENSE("GPL");
MODULE_DESCRIPTION("FLIR Yildun FPGA loader");
MODULE_AUTHOR("Peter Fitger");
