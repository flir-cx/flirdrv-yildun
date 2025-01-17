// SPDX-License-Identifier: GPL-2.0-or-later
#include "flir_kernel_os.h"
#include "yildun_internal.h"
#include <linux/of_gpio.h>
#include <linux/of.h>
#include <linux/regulator/consumer.h>
#include <linux/regulator/of_regulator.h>
#include <linux/platform_device.h>
#include <linux/pinctrl/consumer.h>

static BOOL SetupGpioAccessMX6S(PFVD_DEV_INFO pDev);
static void CleanupGpioMX6S(PFVD_DEV_INFO pDev);
static BOOL GetPinDoneMX6S(PFVD_DEV_INFO pDev);
static BOOL GetPinStatusMX6S(PFVD_DEV_INFO pDev);
static DWORD PutInProgrammingModeMX6S(PFVD_DEV_INFO);
static void BSPFvdPowerDownMX6S(PFVD_DEV_INFO pDev);
static void BSPFvdPowerUpMX6S(PFVD_DEV_INFO pDev);

int SetupMX6S(PFVD_DEV_INFO pDev)
{
	pDev->pSetupGpioAccess = SetupGpioAccessMX6S;
	pDev->pCleanupGpio = CleanupGpioMX6S;
	pDev->pGetPinDone = GetPinDoneMX6S;
	pDev->pGetPinStatus = GetPinStatusMX6S;
	pDev->pPutInProgrammingMode = PutInProgrammingModeMX6S;
	pDev->pBSPFvdPowerUp = BSPFvdPowerUpMX6S;
	pDev->pBSPFvdPowerDown = BSPFvdPowerDownMX6S;

	pDev->iSpiBus = 1;		// SPI no = 1
	pDev->iSpiCountDivisor = 1;	// Count is no of bytes
	return 0;
}

BOOL SetupGpioAccessMX6S(PFVD_DEV_INFO pDev)
{
	struct device *dev = pDev->dev;
	int ret;

	/* FPGA control GPIO */
	pDev->fpga_ce = of_get_named_gpio(dev->of_node, "fpga2-ce-gpio", 0);
	if (gpio_is_valid(pDev->fpga_ce)) {
		ret = devm_gpio_request_one(dev, pDev->fpga_ce, GPIOF_OUT_INIT_LOW, "FPGA2 CE");
		if (ret)
			dev_err(dev, "unable to get FPGA2 CE gpio\n");
	} else {
		dev_err(dev, "can't get gpio fpga2-ce-gpio");
	}

	pDev->fpga_conf_done = of_get_named_gpio(dev->of_node, "fpga2-conf-done-gpio", 0);
	if (gpio_is_valid(pDev->fpga_conf_done)) {
		ret = devm_gpio_request_one(dev, pDev->fpga_conf_done, GPIOF_IN, "FPGA2 CONF_DONE");
		if (ret)
			dev_err(dev, "unable to get FPGA2 CONF_DONE gpio\n");
	} else {
		dev_err(dev, "can't get gpio fpga2-conf-done-gpio");
	}

	pDev->fpga_config = of_get_named_gpio(dev->of_node, "fpga2-config-gpio", 0);
	if (gpio_is_valid(pDev->fpga_config)) {
		ret = devm_gpio_request_one(dev, pDev->fpga_config, GPIOF_OUT_INIT_LOW, "FPGA2 CONFIG");
		if (ret)
			dev_err(dev, "unable to get FPGA2 CONFIG gpio\n");
	} else {
		dev_err(dev, "can't get gpio fpga2-config-gpio");
	}

	pDev->fpga_status = of_get_named_gpio(dev->of_node, "fpga2-status-gpio", 0);
	if (gpio_is_valid(pDev->fpga_status)) {
		ret = devm_gpio_request_one(dev, pDev->fpga_status, GPIOF_IN, "FPGA2 STATUS");
		if (ret)
			dev_err(dev, "unable to get FPGA2 STATUS gpio\n");
	} else {
		dev_err(dev, "can't get gpio fpga2-status-gpio");
	}

	/* SPI GPIO */
	pDev->spi_sclk_gpio = of_get_named_gpio(dev->of_node, "spi2-sclk-gpio", 0);
	if (!gpio_is_valid(pDev->spi_sclk_gpio))
		dev_err(dev, "can't get gpio spi2-sclk-gpio");
	pDev->spi_mosi_gpio = of_get_named_gpio(dev->of_node, "spi2-mosi-gpio", 0);
	if (!gpio_is_valid(pDev->spi_mosi_gpio))
		dev_err(dev, "can't get gpio spi2-mosi-gpio");

	/* FPGA regulators */
	pDev->reg_1v1_fpga = devm_regulator_get(dev, "DA9063_BMEM");
	if (IS_ERR(pDev->reg_1v1_fpga))
		dev_err(dev, "can't get regulator DA9063_BMEM");

	pDev->reg_1v2_fpga = devm_regulator_get(dev, "DA9063_LDO2");
	if (IS_ERR(pDev->reg_1v2_fpga))
		dev_err(dev, "can't get regulator DA9063_LDO2");

	pDev->reg_1v8_fpga = devm_regulator_get(dev, "DA9063_LDO3");
	if (IS_ERR(pDev->reg_1v8_fpga))
		dev_err(dev, "can't get regulator DA9063_LDO3");

	pDev->reg_2v5_fpga = devm_regulator_get(dev, "DA9063_LDO6");
	if (IS_ERR(pDev->reg_2v5_fpga))
		dev_err(dev, "can't get regulator DA9063_LDO6");

	pDev->reg_3v15_fpga = devm_regulator_get(dev, "DA9063_LDO7");
	if (IS_ERR(pDev->reg_3v15_fpga))
		dev_err(dev, "can't get regulator DA9063_LDO7");

	pDev->pinctrl = devm_pinctrl_get(dev);
	if (IS_ERR(pDev->pinctrl))
		dev_err(dev, "can't get pinctrl");

	pDev->pins_default = pinctrl_lookup_state(pDev->pinctrl, "default");
	if (IS_ERR(pDev->pins_default))
		dev_err(dev, "can't get default pins %p %p", pDev->pinctrl, pDev->pins_default);

	pDev->pins_sleep = pinctrl_lookup_state(pDev->pinctrl, "idle");
	if (IS_ERR(pDev->pins_sleep))
		dev_err(dev, "can't get sleep pins %p %p", pDev->pinctrl, pDev->pins_sleep);

	// Yildun initial state shall be off to prevent leakage
	// if standby entered without prior use of Yildun
	pinctrl_select_state(pDev->pinctrl, pDev->pins_sleep);

	if (devm_gpio_request_one(dev, pDev->spi_sclk_gpio, GPIOF_IN, "SPI2_SCLK"))
		dev_err(pDev->dev, "SPI2_SCLK can not be requested\n");

	if (devm_gpio_request_one(dev, pDev->spi_mosi_gpio, GPIOF_IN, "SPI2_MOSI"))
		dev_err(pDev->dev, "SPI2_MOSI can not be requested\n");

	return TRUE;
}

void CleanupGpioMX6S(PFVD_DEV_INFO pDev)
{
}

BOOL GetPinDoneMX6S(PFVD_DEV_INFO pDev)
{
	return (gpio_get_value(pDev->fpga_conf_done) != 0);
}

BOOL GetPinStatusMX6S(PFVD_DEV_INFO pDev)
{
	return (gpio_get_value(pDev->fpga_status) != 0);
}

DWORD PutInProgrammingModeMX6S(PFVD_DEV_INFO pDev)
{
	int tmo = 10;

	// Set idle state (probably already done)
	gpio_set_value(pDev->fpga_config, 1);
	usleep_range(1000, 2000);

	// Activate programming (CONFIG  LOW)
	gpio_set_value(pDev->fpga_config, 0);
	usleep_range(1000, 2000);

	// Verify status
	if (GetPinStatusMX6S(pDev)) {
		dev_err(pDev->dev, "FPGA: Status not initially low\n");
		return 0;
	}

	if (GetPinDoneMX6S(pDev)) {
		dev_err(pDev->dev, "FPGA: Conf_Done not initially low\n");
		return 0;
	}
	// Release config
	gpio_set_value(pDev->fpga_config, 1);
	usleep_range(2000, 5000);

	// Wait for POR to complete
	while (tmo--) {
		if (GetPinStatusMX6S(pDev))
			break;
		usleep_range(5000, 20000);
	}

	// Verify status
	if (!GetPinStatusMX6S(pDev)) {
		dev_err(pDev->dev, "FPGA: Status not high when config released\n");
		return 0;
	}

	return 1;
}

/**
 * This function should apply power to the device.
 *
 *
 * @param pDev
 */
void BSPFvdPowerUpMX6S(PFVD_DEV_INFO pDev)
{
	int ret;

	gpio_free(pDev->spi_sclk_gpio);
	gpio_free(pDev->spi_mosi_gpio);

	// Set SPI as SPI
	ret = pinctrl_select_state(pDev->pinctrl, pDev->pins_default);

	// Power ON
	ret |= regulator_enable(pDev->reg_3v15_fpga);
	ret |= regulator_enable(pDev->reg_2v5_fpga);
	ret |= regulator_enable(pDev->reg_1v2_fpga);
	ret |= regulator_enable(pDev->reg_1v8_fpga);
	ret |= regulator_enable(pDev->reg_1v1_fpga);

	usleep_range(10000, 20000);

	// Release Config
	gpio_set_value(pDev->fpga_ce, 0);
	gpio_set_value(pDev->fpga_config, 1);

	usleep_range(10000, 20000);
}

/**
 * This function should suspend power to the device.
 * It is useful only with devices that can power down under software control.
 *
 *
 * @param pDev
 */
void BSPFvdPowerDownMX6S(PFVD_DEV_INFO pDev)
{
	int ret;

	// Disable FPGA, unconfigure fpga
	gpio_set_value(pDev->fpga_ce, 1);
	gpio_set_value(pDev->fpga_config, 0);

	// Switch off power
	ret = regulator_disable(pDev->reg_3v15_fpga);
	ret |= regulator_disable(pDev->reg_2v5_fpga);
	ret |= regulator_disable(pDev->reg_1v2_fpga);
	ret |= regulator_disable(pDev->reg_1v8_fpga);
	ret |= regulator_disable(pDev->reg_1v1_fpga);

	// Set SPI as GPIO
	ret |= pinctrl_select_state(pDev->pinctrl, pDev->pins_sleep);

	// Set SPI as input
	if (gpio_request(pDev->spi_sclk_gpio, "SPI2_SCLK"))
		dev_err(pDev->dev, "SPI2_SCLK can not be requested\n");
	else
		gpio_direction_input(pDev->spi_sclk_gpio);
	if (gpio_request(pDev->spi_mosi_gpio, "SPI2_MOSI"))
		dev_err(pDev->dev, "SPI2_MOSI can not be requested\n");
	else
		gpio_direction_input(pDev->spi_mosi_gpio);
}
