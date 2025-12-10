// SPDX-License-Identifier: GPL-2.0
/*
 * Nuvoton NUC990 EHCI driver
 *
 * Copyright (c) 2025 Nuvoton Technology Corporation.
 */

#include <linux/kernel.h>
#include <linux/module.h>
#include <linux/clk.h>
#include <linux/dma-mapping.h>
#include <linux/io.h>
#include <linux/of.h>
#include <linux/of_address.h>
#include <linux/of_platform.h>
#include <linux/platform_device.h>
#include <linux/mfd/syscon.h>
#include <linux/regmap.h>
#include <linux/usb.h>
#include <linux/usb/hcd.h>
#include "ehci.h"

#define DRIVER_DESC "Nuvoton NUC990 EHCI driver"

#define REG_SYS_PWRON		0x04
#define USRHDSEN		BIT(11)

#define REG_MISC_FCR		0x30
#define USBID			BIT(16)

#define REG_USBPCR0		0xC4
#define REG_USBPCR1		0xC8

#define REG_OHCI_MISCTRL	0x204
#define MISC_OCAL		BIT(3)

static const char hcd_name[] = "ehci-nuc990";

/* Private data */
#define hcd_to_nuc990_ehci_priv(h) \
	((struct nuc990_ehci_priv *)hcd_to_ehci(h)->priv)

struct nuc990_ehci_priv {
	struct clk *clk;
	u32 oc_active_level;
};

static struct hc_driver __read_mostly ehci_nuc990_hc_driver;

static const struct ehci_driver_overrides ehci_nuc990_drv_overrides __initconst = {
	.extra_priv_size = sizeof(struct nuc990_ehci_priv),
};

static int nuc990_start_ehci(struct platform_device *pdev)
{
	struct usb_hcd *hcd = platform_get_drvdata(pdev);
	struct nuc990_ehci_priv *priv;
	struct device_node *np_ohci, *np_sys;
	struct regmap *sys_regmap;
	void __iomem *ohci_base;
	u32 reg;
	int ret;

	if (!hcd)
		return -ENODEV;

	np_sys = of_parse_phandle(pdev->dev.of_node, "nuvoton,sys", 0);
	if (!np_sys)
		return -ENODEV;

	sys_regmap = syscon_node_to_regmap(np_sys);
	of_node_put(np_sys);

	if (IS_ERR(sys_regmap))
		return PTR_ERR(sys_regmap);

	ret = regmap_update_bits(sys_regmap, REG_MISC_FCR, USRHDSEN, USRHDSEN);
	if (ret) {
		dev_err(&pdev->dev, "Failed to set USRHDSEN\n");
		return ret;
	}

	ret = regmap_update_bits(sys_regmap, REG_SYS_PWRON,  USBID, USBID);
	if (ret) {
		dev_err(&pdev->dev, "Failed to set USBID\n");
		return ret;
	}

	np_ohci = of_parse_phandle(pdev->dev.of_node, "nuvoton,ohci", 0);
	if (!np_ohci)
		return -ENODEV;

	ohci_base = of_iomap(np_ohci, 0);
	of_node_put(np_ohci);
	if (!ohci_base)
		return -ENOMEM;

	priv = hcd_to_nuc990_ehci_priv(hcd);

	reg = readl(ohci_base + REG_OHCI_MISCTRL);
	if (priv->oc_active_level)
		writel(reg & ~MISC_OCAL, ohci_base + REG_OHCI_MISCTRL);
	else
		writel(reg | MISC_OCAL, ohci_base + REG_OHCI_MISCTRL);

	writel(0x160, hcd->regs + REG_USBPCR0);
	writel(0x520, hcd->regs + REG_USBPCR1);

	return 0;
}

static void nuc990_stop_ehci(struct platform_device *pdev)
{
	struct usb_hcd *hcd = platform_get_drvdata(pdev);
	struct nuc990_ehci_priv *priv = hcd_to_nuc990_ehci_priv(hcd);

	dev_dbg(&pdev->dev, "EHCI stop\n");

	/* Must match clk_prepare_enable() */
	clk_disable_unprepare(priv->clk);
}

/* ------------------------------------------------------------------------- */

static int ehci_nuc990_drv_probe(struct platform_device *pdev)
{
	const struct hc_driver *driver = &ehci_nuc990_hc_driver;
	struct nuc990_ehci_priv *priv;
	struct resource *res;
	struct ehci_hcd *ehci;
	struct usb_hcd *hcd;
	int irq;
	int retval;

	if (usb_disabled())
		return -ENODEV;

	dev_info(&pdev->dev, "Initializing NUC990 EHCI\n");

	irq = platform_get_irq(pdev, 0);
	if (irq < 0) {
		retval = irq;
		goto fail;
	}

	/* Set DMA capability */
	retval = dma_coerce_mask_and_coherent(&pdev->dev, DMA_BIT_MASK(32));
	if (retval)
		goto fail;

	/* Create HCD */
	hcd = usb_create_hcd(driver, &pdev->dev, dev_name(&pdev->dev));
	if (!hcd) {
		retval = -ENOMEM;
		goto fail;
	}

	platform_set_drvdata(pdev, hcd);

	/* I/O resource */
	res = platform_get_resource(pdev, IORESOURCE_MEM, 0);
	hcd->regs = devm_ioremap_resource(&pdev->dev, res);
	if (IS_ERR(hcd->regs)) {
		retval = PTR_ERR(hcd->regs);
		goto fail_put;
	}

	hcd->rsrc_start = res->start;
	hcd->rsrc_len   = resource_size(res);

	ehci = hcd_to_ehci(hcd);
	ehci->caps = hcd->regs;

	priv = hcd_to_nuc990_ehci_priv(hcd);

	/* Clocks */
	priv->clk = of_clk_get(pdev->dev.of_node, 0);
	if (IS_ERR(priv->clk)) {
		retval = PTR_ERR(priv->clk);
		dev_err(&pdev->dev, "failed to get clock: %d\n", retval);
		goto fail_put;
	}

	retval = clk_prepare_enable(priv->clk);
	if (retval) {
		dev_err(&pdev->dev, "failed to enable clock: %d\n", retval);
		goto fail_put_clk;
	}

	/* optional: OC level */
	if (of_property_read_u32(pdev->dev.of_node, "oc-active-level",
				 &priv->oc_active_level)) {
		priv->oc_active_level = 0;
		dev_warn(&pdev->dev, "oc-active-level not specified, default=0\n");
	}

	retval = nuc990_start_ehci(pdev);
	if (retval)
		goto fail_clk;

	retval = usb_add_hcd(hcd, irq, IRQF_SHARED);
	if (retval)
		goto fail_clk;

	device_wakeup_enable(hcd->self.controller);
	return 0;

fail_clk:
	clk_disable_unprepare(priv->clk);
fail_put_clk:
	clk_put(priv->clk);
fail_put:
	usb_put_hcd(hcd);
fail:
	dev_err(&pdev->dev, "nuc990 ehci init failed (%d)\n", retval);
	return retval;
}

static int ehci_nuc990_drv_remove(struct platform_device *pdev)
{
	struct usb_hcd *hcd = platform_get_drvdata(pdev);

	usb_remove_hcd(hcd);
	nuc990_stop_ehci(pdev);
	usb_put_hcd(hcd);

	return 0;
}

/* Power management */
static int __maybe_unused ehci_nuc990_drv_suspend(struct device *dev)
{
	struct usb_hcd *hcd = dev_get_drvdata(dev);
	struct nuc990_ehci_priv *priv = hcd_to_nuc990_ehci_priv(hcd);
	int ret;

	ret = ehci_suspend(hcd, false);
	if (ret)
		return ret;

	clk_disable(priv->clk);
	return 0;
}

static int __maybe_unused ehci_nuc990_drv_resume(struct device *dev)
{
	struct usb_hcd *hcd = dev_get_drvdata(dev);
	struct nuc990_ehci_priv *priv = hcd_to_nuc990_ehci_priv(hcd);
	int ret;

	ret = clk_enable(priv->clk);
	if (ret)
		return ret;

	ehci_resume(hcd, false);
	return 0;
}

static SIMPLE_DEV_PM_OPS(ehci_nuc990_pm_ops,
			 ehci_nuc990_drv_suspend,
			 ehci_nuc990_drv_resume);

static const struct of_device_id nuc990_ehci_dt_ids[] = {
	{ .compatible = "nuvoton,nuc990-ehci" },
	{ /* sentinel */ }
};
MODULE_DEVICE_TABLE(of, nuc990_ehci_dt_ids);

static struct platform_driver ehci_nuc990_driver = {
	.probe		= ehci_nuc990_drv_probe,
	.remove		= ehci_nuc990_drv_remove,
	.shutdown	= usb_hcd_platform_shutdown,
	.driver		= {
		.name	= "nuc990-ehci",
		.pm	= &ehci_nuc990_pm_ops,
		.of_match_table = nuc990_ehci_dt_ids,
	},
};

static int __init ehci_nuc990_init(void)
{
	if (usb_disabled())
		return -ENODEV;

	pr_info("%s: %s\n", hcd_name, DRIVER_DESC);
	ehci_init_driver(&ehci_nuc990_hc_driver, &ehci_nuc990_drv_overrides);

	return platform_driver_register(&ehci_nuc990_driver);
}
module_init(ehci_nuc990_init);

static void __exit ehci_nuc990_cleanup(void)
{
	platform_driver_unregister(&ehci_nuc990_driver);
}
module_exit(ehci_nuc990_cleanup);

MODULE_DESCRIPTION(DRIVER_DESC);
MODULE_ALIAS("platform:nuc990-ehci");
MODULE_LICENSE("GPL v2");
