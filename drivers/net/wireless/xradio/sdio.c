/*
 * SDIO driver for XRadio drivers
 *
 * Copyright (c) 2013, XRadio
 * Author: XRadio
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 as
 * published by the Free Software Foundation.
 */

#include <linux/module.h>
#include <linux/mmc/host.h>
#include <linux/mmc/sdio_func.h>
#include <linux/mmc/card.h>
#include <linux/mmc/sdio.h>
#include <linux/of.h>
#include <linux/of_irq.h>
#include <linux/interrupt.h>

#include "xradio.h"
#include "sdio.h"
#include "main.h"
#include "platform.h"

/* sdio vendor id and device id*/
#define SDIO_VENDOR_ID_XRADIO 0x0020
#define SDIO_DEVICE_ID_XRADIO 0x2281
static const struct sdio_device_id xradio_sdio_ids[] = {
	{ SDIO_DEVICE(SDIO_VENDOR_ID_XRADIO, SDIO_DEVICE_ID_XRADIO) },
	{ /* end: all zeroes */			},
};

/* sbus_ops implemetation */
int sdio_data_read(struct xradio_common* self, unsigned int addr,
                          void *dst, int count)
{
	int ret = sdio_memcpy_fromio(self->sdio_func, dst, addr, count);
//	printk("sdio_memcpy_fromio 0x%x:%d ret %d\n", addr, count, ret);
//	print_hex_dump_bytes("sdio read ", 0, dst, min(count,32));
	return ret;
}

int sdio_data_write(struct xradio_common* self, unsigned int addr,
                           const void *src, int count)
{
	int ret = sdio_memcpy_toio(self->sdio_func, addr, (void *)src, count);
//	printk("sdio_memcpy_toio 0x%x:%d ret %d\n", addr, count, ret);
//	print_hex_dump_bytes("sdio write", 0, src, min(count,32));
	return ret;
}

void sdio_lock(struct xradio_common* self)
{
	sdio_claim_host(self->sdio_func);
}

void sdio_unlock(struct xradio_common *self)
{
	sdio_release_host(self->sdio_func);
}

size_t sdio_align_len(struct xradio_common *self, size_t size)
{
	return sdio_align_size(self->sdio_func, size);
}

int sdio_set_blk_size(struct xradio_common *self, size_t size)
{
	return sdio_set_block_size(self->sdio_func, size);
}

extern void xradio_irq_handler(struct xradio_common*);

#ifndef CONFIG_XRADIO_USE_GPIO_IRQ
static void sdio_irq_handler(struct sdio_func *func)
{
	struct xradio_common *self = sdio_get_drvdata(func);
	if (self != NULL)
		xradio_irq_handler(self);
}
#else
static irqreturn_t sdio_irq_handler(int irq, void *dev_id)
{
	struct sdio_func *func = (struct sdio_func*) dev_id;
	struct xradio_common *self = sdio_get_drvdata(func);
	if (self != NULL)
		xradio_irq_handler(self);
	return IRQ_HANDLED;
}

static int sdio_enableint(struct sdio_func* func)
{
	int ret = 0;
	u8 cccr;
	int func_num;

	sdio_claim_host(func);

	/* Hack to access Fuction-0 */
	func_num = func->num;
	func->num = 0;
	cccr = sdio_readb(func, SDIO_CCCR_IENx, &ret);
	cccr |= BIT(0); /* Master interrupt enable ... */
	cccr |= BIT(func_num); /* ... for our function */
	sdio_writeb(func, cccr, SDIO_CCCR_IENx, &ret);

	/* Restore the WLAN function number */
	func->num = func_num;

	sdio_release_host(func);

	return ret;
}
#endif

int sdio_pm(struct xradio_common *self, bool  suspend)
{
	int ret = 0;
	if (suspend) {
		/* Notify SDIO that XRADIO will remain powered during suspend */
		ret = sdio_set_host_pm_flags(self->sdio_func, MMC_PM_KEEP_POWER);
		if (ret)
			xr_printk(XRADIO_DBG_WARN, "SDIO: Error setting SDIO pm flags #%i\n", ret);
	}

	return ret;
}

/* Probe Function to be called by SDIO stack when device is discovered */
static int sdio_probe(struct sdio_func *func,
                      const struct sdio_device_id *id)
{
	int ret = 0;

	xr_printk(XRADIO_DBG_ALWY, "XR819 device discovered\n");
	xr_printk(XRADIO_DBG_MSG, "SDIO: clock  = %d\n", func->card->host->ios.clock);
	xr_printk(XRADIO_DBG_MSG, "SDIO: class  = %x\n", func->class);
	xr_printk(XRADIO_DBG_MSG, "SDIO: vendor = 0x%04x\n", func->vendor);
	xr_printk(XRADIO_DBG_MSG, "SDIO: device = 0x%04x\n", func->device);
	xr_printk(XRADIO_DBG_MSG, "SDIO: fctn#  = 0x%04x\n", func->num);


	func->card->quirks |= MMC_QUIRK_BROKEN_BYTE_MODE_512;
	sdio_claim_host(func);
	sdio_enable_func(func);
#ifndef CONFIG_XRADIO_USE_GPIO_IRQ
	ret = sdio_claim_irq(func, sdio_irq_handler);
	if (ret) {
		xr_printk(XRADIO_DBG_ERROR, "%s:sdio_claim_irq failed(%d).\n", __func__, ret);
		sdio_release_host(func);
		return ret;
	}
#else
	xradio_request_gpio_irq(func, sdio_irq_handler);
	sdio_enableint(func);
#endif
	sdio_release_host(func);
	xradio_core_init(func);

	return 0;
}
/* Disconnect Function to be called by SDIO stack when
 * device is disconnected */
static void sdio_remove(struct sdio_func *func)
{
	struct mmc_card *card = func->card;
#ifndef CONFIG_XRADIO_USE_GPIO_IRQ
	sdio_claim_host(func);
	sdio_release_irq(func);
	sdio_release_host(func);
#else
	xradio_free_gpio_irq(func);
#endif
	xradio_core_deinit(func);
	sdio_claim_host(func);
	sdio_disable_func(func);
	mmc_hw_reset(card->host);
	sdio_release_host(func);
}

static int sdio_suspend(struct device *dev)
{
	int ret = 0;
	/*
	struct sdio_func *func = dev_to_sdio_func(dev);
	ret = sdio_set_host_pm_flags(func, MMC_PM_KEEP_POWER);
	if (ret)
		sbus_printk(XRADIO_DBG_ERROR, "set MMC_PM_KEEP_POWER error\n");
	*/
	return ret;
}

static int sdio_resume(struct device *dev)
{
	return 0;
}

static const struct dev_pm_ops sdio_pm_ops = {
	.suspend = sdio_suspend,
	.resume  = sdio_resume,
};

static struct sdio_driver sdio_driver = {
	.name     = "xradio_wlan",
	.id_table = xradio_sdio_ids,
	.probe    = sdio_probe,
	.remove   = sdio_remove,
	.drv = {
			.owner = THIS_MODULE,
			.pm = &sdio_pm_ops,
	}
};

int xradio_sdio_register() {
	int ret = 0;

	xr_printk(XRADIO_DBG_MSG, "%s\n", __func__);
	
	xradio_wlan_power(1);
	ret = sdio_register_driver(&sdio_driver);
	if (ret) {
		xr_printk(XRADIO_DBG_ERROR, "sdio_register_driver failed!\n");
		return ret;
	}
	xradio_sdio_detect(1);

	return ret;
}

void xradio_sdio_unregister(){
	xr_printk(XRADIO_DBG_MSG, "%s\n", __func__);

	xradio_wlan_power(0);
	sdio_unregister_driver(&sdio_driver);
	xradio_sdio_detect(0);
}

MODULE_DEVICE_TABLE(sdio, xradio_sdio_ids);
