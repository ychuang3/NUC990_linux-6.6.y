// SPDX-License-Identifier: GPL-2.0
/*
 * Nuvoton NUC990 Window Watchdog Timer (WWDT) driver
 *
 * Copyright (c) 2025 Nuvoton Technology Corporation.
 */

#include <linux/bitops.h>
#include <linux/errno.h>
#include <linux/io.h>
#include <linux/clk.h>
#include <linux/kernel.h>
#include <linux/module.h>
#include <linux/platform_device.h>
#include <linux/interrupt.h>
#include <linux/types.h>
#include <linux/watchdog.h>
#include <linux/of.h>
#include <linux/of_address.h>

/* WWDT registers  */
#define WWDT_RLDCNT	0x00
#define WWDT_CTL	0x04
#define WWDT_STATUS	0x08
#define WWDT_CNT	0x0C

/* WWDT_CTL bits */
#define WWDTEN		BIT(0)
#define INTEN		BIT(1)
#define PSCSEL_MASK	(0xFul << 8)
#define CMPDAT_MASK	(0x3Ful << 16)
#define ICEDEBUG	BIT(31)

/* WWDT_STATUS bits */
#define WWDTIF		BIT(0) /* write 1 to clear */
#define WWDTRF		BIT(1) /* write 1 to clear */

/* Global SYS register offsets */
#define SYS_WRPRTR	0x1a0

/* Reload word */
#define RELOAD_WORD	0x00005AA5

#define WWDT_CONFIG_BASE	0x00200F01

struct nuc990_wwdt {
	void __iomem *base;
	void __iomem *sys_base;
	struct clk *clk;
	struct watchdog_device wdd;
};

/* SYS write-protect unlock/lock */
static void sys_unlock(struct nuc990_wwdt *wdt)
{
	void __iomem *wrpr = wdt->sys_base + SYS_WRPRTR;

	do {
		writel(0x59, wrpr);
		writel(0x16, wrpr);
		writel(0x88, wrpr);
	} while (!(readl(wrpr) & BIT(0)));
}

static void sys_lock(struct nuc990_wwdt *wdt)
{
	writel(0x0, wdt->sys_base + SYS_WRPRTR);
}

static inline void wwdt_reload(struct nuc990_wwdt *wdt)
{
	writel(RELOAD_WORD, wdt->base + WWDT_RLDCNT);
}

static int nuc990wwdt_ping(struct watchdog_device *wdd)
{
	struct nuc990_wwdt *wdt = watchdog_get_drvdata(wdd);

	wwdt_reload(wdt);
	return 0;
}

static int nuc990wwdt_start(struct watchdog_device *wdd)
{
	struct nuc990_wwdt *wdt = watchdog_get_drvdata(wdd);
	unsigned long flags;
	u32 ctl;

	ctl = WWDT_CONFIG_BASE;

	local_irq_save(flags);
	sys_unlock(wdt);

	writel(WWDTIF | WWDTRF, wdt->base + WWDT_STATUS);

	writel(ctl, wdt->base + WWDT_CTL);

	sys_lock(wdt);
	local_irq_restore(flags);

	wwdt_reload(wdt);

	return 0;
}

static int nuc990wwdt_stop(struct watchdog_device *wdd)
{
	return -EBUSY;
}

static const struct watchdog_info nuc990wwdt_info = {
	.identity = "nuc990 window watchdog",
	.options  = WDIOF_KEEPALIVEPING,
};

static const struct watchdog_ops nuc990wwdt_ops = {
	.owner        = THIS_MODULE,
	.start        = nuc990wwdt_start,
	.stop         = nuc990wwdt_stop,
	.ping         = nuc990wwdt_ping,
};

static int nuc990wwdt_probe(struct platform_device *pdev)
{
	struct device *dev = &pdev->dev;
	struct nuc990_wwdt *wdt;
	struct device_node *np = dev->of_node;
	struct device_node *sys_np;
	u32 st;
	int ret;

	wdt = devm_kzalloc(dev, sizeof(*wdt), GFP_KERNEL);
	if (!wdt)
		return -ENOMEM;

	platform_set_drvdata(pdev, wdt);

	wdt->base = devm_platform_ioremap_resource(pdev, 0);
	if (IS_ERR(wdt->base))
		return PTR_ERR(wdt->base);

	sys_np = of_parse_phandle(np, "nuvoton,sys", 0);
	if (!sys_np)
		return dev_err_probe(dev, -EINVAL, "missing nuvoton,sys phandle\n");

	wdt->sys_base = of_iomap(sys_np, 0);
	of_node_put(sys_np);
	if (!wdt->sys_base)
		return dev_err_probe(dev, -ENOMEM, "failed to map sys registers\n");

	ret = devm_add_action_or_reset(dev, (void(*)(void *))iounmap, wdt->sys_base);
	if (ret)
		return ret;

	/* Obtain WWDT clock from DT */
	wdt->clk = devm_clk_get(dev, "wwdt0_gate");
	if (IS_ERR(wdt->clk))
		return dev_err_probe(dev, PTR_ERR(wdt->clk),
				     "failed to get wwdt0_gate clock\n");

	ret = clk_prepare_enable(wdt->clk);
	if (ret)
		return dev_err_probe(dev, ret, "failed to enable wwdt clock\n");

	/* Setup watchdog_device (per-device) */
	wdt->wdd.info   = &nuc990wwdt_info;
	wdt->wdd.ops    = &nuc990wwdt_ops;
	wdt->wdd.parent = dev;

	/*
	 * Keep legacy semantics: fixed config -> fixed nominal timeout.
	 * (This is "window watchdog"; user must ping inside window.)
	 */
	wdt->wdd.timeout = 2;
	wdt->wdd.min_timeout = 1;
	wdt->wdd.max_timeout = 2;

	/* No way out once started */
	watchdog_set_nowayout(&wdt->wdd, true);

	watchdog_set_drvdata(&wdt->wdd, wdt);

	/* bootstatus: check reset flag */
	st = readl(wdt->base + WWDT_STATUS);
	if (st & WWDTRF)
		wdt->wdd.bootstatus = WDIOF_CARDRESET;

	ret = watchdog_register_device(&wdt->wdd);
	if (ret) {
		clk_disable_unprepare(wdt->clk);
		return ret;
	}

	return 0;
}

static int nuc990wwdt_remove(struct platform_device *pdev)
{
	struct nuc990_wwdt *wdt = platform_get_drvdata(pdev);

	/*
	 * WWDT is "no way out": even if driver unloads, hardware keeps running
	 * once started. We still unregister the char device but not disable clock.
	 */
	watchdog_unregister_device(&wdt->wdd);

	return 0;
}

static const struct of_device_id nuc990_wwdt_of_match[] = {
	{ .compatible = "nuvoton,nuc990-wwdt" },
	{ }
};
MODULE_DEVICE_TABLE(of, nuc990_wwdt_of_match);

static struct platform_driver nuc990wwdt_driver = {
	.probe  = nuc990wwdt_probe,
	.remove = nuc990wwdt_remove,
	.driver = {
		.name = "nuc990-wwdt",
		.of_match_table = nuc990_wwdt_of_match,
	},
};

module_platform_driver(nuc990wwdt_driver);

MODULE_DESCRIPTION("NUC990 Window Watchdog Timer (WWDT) driver");
MODULE_LICENSE("GPL");
