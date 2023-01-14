/*
 * platform interfaces for XRadio drivers
 *
 * Copyright (c) 2013, XRadio
 * Author: XRadio
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 as
 * published by the Free Software Foundation.
 */
#include <linux/version.h>
#include <linux/module.h>
#include <linux/err.h>
#include <linux/clk.h>
#include <linux/delay.h>
#include <linux/interrupt.h>
#include <linux/gpio.h>
#include <linux/ioport.h>
#include <linux/regulator/consumer.h>
#include <linux/sunxi-gpio.h>
#include <linux/gpio.h>
#include <linux/types.h>
#include <linux/power/aw_pm.h>
#include <mach/sunxi-chip.h>

#include "xradio.h"
#include "platform.h"
#include "sdio.h"

#define CHIP_SIZE	16

extern void sunxi_wlan_set_power(bool on);
extern int sunxi_wlan_get_bus_index(void);
extern int sunxi_wlan_get_oob_irq(void);
extern int sunxi_wlan_get_oob_irq_flags(void);
extern void sunxi_wlan_chipid_mac_address(u8 *mac);

int xradio_wlan_power(int on)
{
	sunxi_wlan_set_power(on);
	mdelay(100);
	return 0;
}

void xradio_sdio_detect(int enable)
{
	int wlan_bus_id = sunxi_wlan_get_bus_index();
	MCI_RESCAN_CARD(wlan_bus_id);
	xr_printk(XRADIO_DBG_ALWY, "%s SDIO card %d\n", enable ? "Detect" : "Remove", wlan_bus_id);
	mdelay(10);
}

int xradio_request_gpio_irq(struct sdio_func *func, irq_handler_t handler)
{
	struct device *dev = &func->dev;
	int ret = -1;

	int irq = sunxi_wlan_get_oob_irq();
	if (!irq) {
		xr_printk(XRADIO_DBG_ERROR, "SDIO: No irq in platform data\n");
		return -EINVAL;
	}

	ret = devm_request_irq(dev, irq, handler, 0, "xradio", func);
	if (ret) {
		xr_printk(XRADIO_DBG_ERROR, "SDIO: Failed to request irq_wakeup.\n");
		return -EINVAL;
	}

	return ret;
}

void xradio_free_gpio_irq(struct sdio_func *func)
{
	struct device *dev = &func->dev;

	int irq = sunxi_wlan_get_oob_irq();
	if (!irq) {
		return;
	}

	disable_irq(irq);
	devm_free_irq(dev, irq, func);
}

void xradio_get_mac(u8 *mac) {
	u8 serial[CHIP_SIZE];
	sunxi_get_serial((u8 *)serial);

	mac[0] = 0xDC;
	mac[1] = 0x44;
	mac[2] = 0x6D;
	mac[3] = serial[0];
	mac[4] = serial[1];
	mac[5] = serial[2];
}