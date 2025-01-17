// SPDX-License-Identifier: GPL-2.0-or-later
/***********************************************************************
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
#include "yildun_internal.h"
#include <linux/platform_device.h>
#include <linux/of.h>
#include <yildundev.h>
#include <linux/dma-mapping.h>
#include <linux/miscdevice.h>

static int init(struct device *dev);
static void deinit(struct device *dev);
static int yildun_probe(struct platform_device *pdev);
static int yildun_remove(struct platform_device *pdev);
static long ioctl(struct file *filep, unsigned int cmd, unsigned long arg);

struct yildun_data {
	FVD_DEV_INFO yildundev;
	struct miscdevice miscdev;
	struct device *dev;
	int enabled;
};

static const struct file_operations yildun_misc_fops = {
	.owner = THIS_MODULE,
	.unlocked_ioctl = ioctl,
	/* .open = yildun_open, */
	/* .mmap = yildun_mmap, */
};

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
 * Yildun_Init
 *
 *
 * @return
 */
static int init(struct device *dev)
{
	struct yildun_data *data = dev_get_drvdata(dev);
	int retval = -1;

	dev_info(dev, "Yildun Init\n");

	dev->dma_mask = devm_kmalloc(dev, sizeof(*dev->dma_mask), GFP_KERNEL);

	if (!dev->dma_mask)
		return -ENOMEM;

	*dev->dma_mask = DMA_BIT_MASK(32);
	dev->coherent_dma_mask = DMA_BIT_MASK(32);

	retval = SetupMX6S(&data->yildundev);
	if (retval) {
		dev_err(dev, "Error initializing MX6S for Yildun\n");
		return retval;
	}

	if (!data->yildundev.pSetupGpioAccess) {
		dev_err(dev, "Error creating Yildun class\n");
		goto OUT_CLASSCREATE;
	}

	if (!data->yildundev.pSetupGpioAccess(&data->yildundev)) {
		dev_err(dev, "Error setting up GPIO\n");
		goto OUT_DEVICECREATE;
	}

	return 0;

OUT_DEVICECREATE:
	data->yildundev.pCleanupGpio(&data->yildundev);
OUT_CLASSCREATE:
	data->yildundev.pBSPFvdPowerDown(&data->yildundev);
	return retval;
}

/**
 * Yildun_Deinit
 *
 */
static void deinit(struct device *dev)
{
	struct yildun_data *data = dev_get_drvdata(dev);
	data->yildundev.pCleanupGpio(&data->yildundev);
}

static int yildun_probe(struct platform_device *pdev)
{
	int ret;
	struct device *dev = &pdev->dev;

	struct yildun_data *data = devm_kzalloc(dev, sizeof(struct yildun_data), GFP_KERNEL);
	if (!data)
		return -ENOMEM;

	platform_set_drvdata(pdev, data);

	data->yildundev.dev = dev;
	data->dev = dev;
	data->miscdev.minor = MISC_DYNAMIC_MINOR;
	data->miscdev.name = devm_kasprintf(dev, GFP_KERNEL, "yildun");
	data->miscdev.fops = &yildun_misc_fops;
	data->miscdev.parent = dev;

	ret = misc_register(&data->miscdev);
	if (ret) {
		dev_err(dev, "Failed to register miscdev for Yildun driver\n");
		//goto ERROR_MISC_REGISTER;
		//TODO: Fail!!
	}

	return init(dev);
}

static int yildun_remove(struct platform_device *pdev)
{
	struct yildun_data *data = platform_get_drvdata(pdev);
	struct device *dev = &pdev->dev;

	deinit(dev);
	misc_deregister(&data->miscdev);
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
	struct yildun_data *data = container_of(filep->private_data, struct yildun_data, miscdev);
	int ret = 0;

	switch (cmd) {
	case IOCTL_YILDUN_ENABLE:
		dev_dbg(data->dev, "IOCTL_YILDUN_ENABLE\n");
		if (!data->enabled) {
			data->yildundev.pBSPFvdPowerUp(&data->yildundev);
			ret = LoadFPGA(&data->yildundev);
			if (ret) {
				data->yildundev.pBSPFvdPowerDown(&data->yildundev);
				dev_err(data->dev, "Enable Yildun FPGA (IOCTL_YILDUN_ENABLE) failed: %d\n", ret);
			} else {
				data->enabled = TRUE;
			}
		}
		break;

	case IOCTL_YILDUN_DISABLE:
		dev_dbg(data->dev, "IOCTL_YILDUN_DISABLE\n");
		if (data->enabled) {
			data->yildundev.pBSPFvdPowerDown(&data->yildundev);
			data->enabled = FALSE;
		}
		break;

	default:
		dev_dbg(data->dev, "Yildun Ioctl %X Not supported\n", cmd);
		ret = -ERROR_NOT_SUPPORTED;
		break;
	}

	return ret;
}

module_platform_driver(yildun_driver);

MODULE_LICENSE("GPL");
MODULE_DESCRIPTION("FLIR Yildun FPGA loader");
MODULE_AUTHOR("Peter Fitger");
