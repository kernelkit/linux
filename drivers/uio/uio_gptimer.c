// SPDX-License-Identifier: GPL-2.0-only
/*
 * UIO driver for TI GPTimer
 *
 * Copyright (C) 2021 Linutronix GmbH
 * Author Martin Kaistra <martin.kaistra@linutronix.de>
 *
 */

#include <linux/device.h>
#include <linux/module.h>
#include <linux/pci.h>
#include <linux/slab.h>
#include <linux/uio_driver.h>
#include <linux/io.h>
#include <linux/of_device.h>
#include <linux/platform_device.h>
#include <linux/platform_data/dmtimer-omap.h>
#include <clocksource/timer-ti-dm.h>

#include <asm/io.h>
#define DRIVER_NAME_GP "uio_gptimer"

/*
*	NOTE:	The UIO is redesigned to only work with timer10, the reason for this is that we now get
*			a better design where it is possible to utilize the Hardware timer to get a 24+32bit timer.
*			This will provide a timer solution that is long enough for the product life cycle and
*			is possible to handle as a "emulated" 64bit hardware timer. By doing this with the
*			TOWR/TOCR register in combination with the TCRR register we remove all sync issues
*			between primary memory and the GP-timers.
*			Using the combination of TOCR + TCRR register, we will get roughly 100 years of counter value
*			(for each boot). A note here is that the wrapper value for TCRR is roughly 3min (20Mhz clock config).
*
*
*			TCLR - 0x38, config (read back for Auto reaload/start stop controll)
*			TCRR - 0x3C, Timer counter (lower 32b, reload value can be set by TTGR)
*			TOWR - 0x6C, 24bit wrapping value for timer reg 1/2/10 (used for upper 23bit), set to 0xFFFFFF
*			TOCR - 0x68, 24 bit overflow counter value
*			Functionaluty by settingthe TOWR register will be that we mask the TCRR interrupt untill TOCR overflows
*			for our logic this will not change anything.
*
*/
#define UIO_GP_TOWR_V 0xFFFFFF //TOWR value for pushing timer10 interrupt masking 24bits.

struct gptimer_uio_platdata {
	struct uio_info *uioinfo;
	struct platform_device *pdev;
	struct omap_dm_timer *odt;
};

static irqreturn_t gptimer_uio_handler(int irq, struct uio_info *info)
{
	struct gptimer_uio_platdata *priv = info->priv;
	__omap_dm_timer_write_status(priv->odt, OMAP_TIMER_INT_OVERFLOW);
	return IRQ_HANDLED;
}

static int gptimer_uio_probe(struct platform_device *pdev)
{
	const struct omap_dm_timer_ops *timer_ops;
	struct dmtimer_platform_data *tpdata;
	struct gptimer_uio_platdata *priv;
	struct device *dev = &pdev->dev;
	struct device_node *np = NULL;
	struct platform_device *tpdev;
	struct omap_dm_timer *odt;
	struct uio_info *info;
	struct resource *mem; //Memory area for the GP-timer objects read from DTC and maps the GP-HW-block

	np = of_parse_phandle(dev->of_node, "ti,timers", 0);
	tpdev = of_find_device_by_node(np);
	if (!tpdev)
		return -ENODEV;

	tpdata = dev_get_platdata(&tpdev->dev);
	timer_ops = tpdata->timer_ops;

	odt = timer_ops->request_by_node(np);
	timer_ops->set_source(odt, OMAP_TIMER_SRC_SYS_CLK);

	__omap_dm_timer_load_start(odt, OMAP_TIMER_CTRL_ST | OMAP_TIMER_CTRL_AR, 0,
			OMAP_TIMER_NONPOSTED);

	__omap_dm_timer_int_enable(odt, OMAP_TIMER_INT_OVERFLOW);

	//If future timers are required then this must be moved to a "if/else" based on timer.
	//This functionality is ONLY! supported on timer 1/2/10 on AM574xx
	__omap_dm_timer_write(odt,OMAP_TIMER_TICK_INT_MASK_COUNT_REG,UIO_GP_TOWR_V, OMAP_TIMER_POSTED);

	mem = platform_get_resource(tpdev, IORESOURCE_MEM, 0);
	if (unlikely(!mem)) {
		dev_err(dev, "%s: no memory resource.\n", __func__);
		return -ENODEV;
	}

	priv = devm_kzalloc(&pdev->dev, sizeof(*priv), GFP_KERNEL);
	if (!priv)
		return -ENOMEM;

	info = kzalloc(sizeof(struct uio_info), GFP_KERNEL);
	if (!info)
		return -ENOMEM;

	priv->uioinfo = info;
	priv->pdev = pdev;
	priv->odt = odt;

	info->mem[0].addr = mem->start;
	if (!info->mem[0].addr)
		goto out_release;
	info->mem[0].size = PAGE_ALIGN(resource_size(mem));
	info->mem[0].memtype = UIO_MEM_PHYS;
	info->mem[0].name = "TIMER";

	info->priv = priv;
	info->irq = timer_ops->get_irq(odt);
	info->irq_flags = IRQF_TIMER;
	info->handler = gptimer_uio_handler;
	info->name = "gptimer_uio";
	info->version = "0.1.0";


	if (uio_register_device(&pdev->dev, info))
		goto out_release;

	return 0;
out_release:
	kfree(info);
	return -ENODEV;
}

static int gptimer_uio_remove(struct platform_device *pdev)
{
	struct gptimer_uio_platdata *priv = platform_get_drvdata(pdev);

	uio_unregister_device(priv->uioinfo);

	return 0;
}

static const struct of_device_id gptimer_uio_of_match[] = {
        { .compatible = "ti,gptimer-uio" },
        {}
};
MODULE_DEVICE_TABLE(of, gptimer_uio_of_match);

static struct platform_driver gptimer_uio_driver = {
	.driver = {
		.name = DRIVER_NAME_GP,
		.of_match_table = of_match_ptr(gptimer_uio_of_match),
	},
	.probe = gptimer_uio_probe,
	.remove = gptimer_uio_remove,
};
module_platform_driver(gptimer_uio_driver);

MODULE_LICENSE("GPL v2");
MODULE_AUTHOR("Martin Kaistra, Carl-Oscar Varnander");
