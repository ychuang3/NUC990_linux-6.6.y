// SPDX-License-Identifier: GPL-2.0
/*
 * Nuvoton NUC990 Watchdog Timer driver
 *
 * Copyright (c) 2025 Nuvoton Technology Corporation.
 */

#include <linux/bitops.h>
#include <linux/errno.h>
#include <linux/init.h>
#include <linux/io.h>
#include <linux/clk.h>
#include <linux/kernel.h>
#include <linux/module.h>
#include <linux/moduleparam.h>
#include <linux/platform_device.h>
#include <linux/slab.h>
#include <linux/interrupt.h>
#include <linux/types.h>
#include <linux/watchdog.h>
#include <linux/of.h>
#include <linux/of_address.h>

/* WDT registers (offset from WDT base) */
#define WDT_CTL		0x00
#define WDT_ALTCTL	0x04
#define WDT_RSTCNT	0x08

/* Global SYS register offsets */
#define SYS_MISCFCR	0x30
#define WDTRSTEN	BIT(8)
#define SYS_WRPRTR	0x1a0

/* WDT Control bits */
#define TOUTSEL		(0x07 << 8)
#define WDTEN		BIT(7)
#define INTEN		BIT(6)
#define WKF		BIT(5)
#define WKEN		BIT(4)
#define IF		BIT(3)
#define RSTF		BIT(2)
#define RSTEN		BIT(1)

#define RESET_COUNTER	0x5AA5
#define WDT_HEARTBEAT	11

static int heartbeat = WDT_HEARTBEAT;
module_param(heartbeat, int, 0);

static bool nowayout = WATCHDOG_NOWAYOUT;
module_param(nowayout, bool, 0);

/* Per-device structure */
struct nuc990_wdt {
	void __iomem *base;      /* WDT registers */
	void __iomem *sys_base;  /* SYS controller registers */
	struct clk *clk;
	bool wakeup;
};

static struct nuc990_wdt *nuc990_wdt;

/*
 * Write-protect unlock/lock via SYS node
 */
static void sys_unlock(struct nuc990_wdt *wdt)
{
	void __iomem *wrpr = wdt->sys_base + SYS_WRPRTR;

	do {
		writel(0x59, wrpr);
		writel(0x16, wrpr);
		writel(0x88, wrpr);
	} while (!(readl(wrpr) & 1));
}

static void sys_lock(struct nuc990_wdt *wdt)
{
	writel(0x0, wdt->sys_base + SYS_WRPRTR);
}

static int nuc990wdt_ping(struct watchdog_device *wdd)
{
	struct nuc990_wdt *wdt = nuc990_wdt;

	writel(RESET_COUNTER, wdt->base + WDT_RSTCNT);
	return 0;
}

static int nuc990wdt_start(struct watchdog_device *wdd)
{
	struct nuc990_wdt *wdt = nuc990_wdt;
	unsigned long flags;
	u32 val = WDTEN | RSTEN;

	if (wdt->wakeup)
		val |= INTEN | WKEN;

	if (wdd->timeout < 2)
		val |= 0x5 << 8;
	else if (wdt->wakeup ? (wdd->timeout < 8) : (wdd->timeout < 11))
		val |= 0x6 << 8;
	else
		val |= 0x7 << 8;

	local_irq_save(flags);
	sys_unlock(wdt);
	writel(readl(wdt->sys_base + SYS_MISCFCR) | WDTRSTEN, wdt->sys_base + SYS_MISCFCR);
	writel(val, wdt->base + WDT_CTL);
	sys_lock(wdt);
	local_irq_restore(flags);

	writel(RESET_COUNTER, wdt->base + WDT_RSTCNT);

	return 0;
}

static int nuc990wdt_stop(struct watchdog_device *wdd)
{
	struct nuc990_wdt *wdt = nuc990_wdt;
	unsigned long flags;

	pr_warn("Stopping hardware watchdog is generally unsafe.\n");

	local_irq_save(flags);
	sys_unlock(wdt);
	writel(0, wdt->base + WDT_CTL);
	sys_lock(wdt);
	local_irq_restore(flags);

	return 0;
}

static int nuc990wdt_set_timeout(struct watchdog_device *wdd, unsigned int t)
{
	struct nuc990_wdt *wdt = nuc990_wdt;
	unsigned long flags;
	u32 val = readl(wdt->base + WDT_CTL);

	val &= ~TOUTSEL;

	if (t < 2)
		val |= 0x5 << 8;
	else if (wdt->wakeup ? (t < 8) : (t < 11))
		val |= 0x6 << 8;
	else
		val |= 0x7 << 8;

	local_irq_save(flags);
	sys_unlock(wdt);
	writel(val, wdt->base + WDT_CTL);
	sys_lock(wdt);
	local_irq_restore(flags);

	wdd->timeout = t;
	return 0;
}

static const struct watchdog_info nuc990wdt_info = {
	.identity = "nuc990 watchdog",
	.options  = WDIOF_SETTIMEOUT | WDIOF_KEEPALIVEPING | WDIOF_MAGICCLOSE,
};

static const struct watchdog_ops nuc990wdt_ops = {
	.owner = THIS_MODULE,
	.start = nuc990wdt_start,
	.stop  = nuc990wdt_stop,
	.ping  = nuc990wdt_ping,
	.set_timeout = nuc990wdt_set_timeout,
};

static struct watchdog_device nuc990_wdd = {
	.info = &nuc990wdt_info,
	.ops  = &nuc990wdt_ops,
	.status = WATCHDOG_NOWAYOUT_INIT_STATUS,
};

static int nuc990wdt_probe(struct platform_device *pdev)
{
	struct device *dev = &pdev->dev;
	struct nuc990_wdt *wdt;
	struct device_node *np = dev->of_node;
	struct device_node *sys_np;
	int ret;

	wdt = devm_kzalloc(dev, sizeof(*wdt), GFP_KERNEL);
	if (!wdt)
		return -ENOMEM;

	nuc990_wdt = wdt;
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

	wdt->wakeup = of_property_read_bool(np, "nuvoton,wdt-wakeup");

	if (wdt->wakeup && heartbeat == WDT_HEARTBEAT)
		heartbeat = 8;

	wdt->clk = devm_clk_get(dev, "wdt0_gate");
	if (IS_ERR(wdt->clk))
		return dev_err_probe(dev, PTR_ERR(wdt->clk),
				     "failed to get wdt0_gate clock\n");

	ret = clk_prepare_enable(wdt->clk);
	if (ret)
		return dev_err_probe(dev, ret,
				     "failed to enable watchdog clock\n");

	/* Timeout ranges */
	if (wdt->wakeup) {
		nuc990_wdd.timeout = 8;
		nuc990_wdd.max_timeout = 8;
	} else {
		nuc990_wdd.timeout = 11;
		nuc990_wdd.max_timeout = 11;
	}
	nuc990_wdd.min_timeout = 1;

	watchdog_init_timeout(&nuc990_wdd, heartbeat, dev);
	watchdog_set_nowayout(&nuc990_wdd, nowayout);
	nuc990_wdd.parent = dev;

	ret = watchdog_register_device(&nuc990_wdd);
	if (ret) {
		clk_disable_unprepare(wdt->clk);
		return ret;
	}

	return 0;
}

static int nuc990wdt_remove(struct platform_device *pdev)
{
	watchdog_unregister_device(&nuc990_wdd);
	clk_disable_unprepare(nuc990_wdt->clk);
	return 0;
}

static const struct of_device_id nuc990_wdt_of_match[] = {
	{ .compatible = "nuvoton,nuc990-wdt" },
	{ }
};
MODULE_DEVICE_TABLE(of, nuc990_wdt_of_match);

static struct platform_driver nuc990wdt_driver = {
	.probe  = nuc990wdt_probe,
	.remove = nuc990wdt_remove,
	.driver = {
		.name = "nuc990-wdt",
		.of_match_table = nuc990_wdt_of_match,
	},
};

module_platform_driver(nuc990wdt_driver);

MODULE_DESCRIPTION("NUC990 Watchdog Timer driver");
MODULE_LICENSE("GPL");
