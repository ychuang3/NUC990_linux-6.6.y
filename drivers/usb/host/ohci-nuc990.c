// SPDX-License-Identifier: GPL-2.0
/*
 * Nuvoton NUC990 OHCI driver
 *
 * Copyright (c) 2025 Nuvoton Technology Corporation.
 */

#include <linux/kernel.h>
#include <linux/module.h>
#include <linux/clk.h>
#include <linux/dma-mapping.h>
#include <linux/io.h>
#include <linux/of.h>
#include <linux/platform_device.h>
#include <linux/usb.h>
#include <linux/usb/hcd.h>
#include "ohci.h"

#define DRIVER_DESC "Nuvoton NUC990 OHCI driver"

static const char hcd_name[] = "ohci-nuc990";

struct nuc990_ohci_priv {
	struct clk *clk;
};

#define hcd_to_nuc990_ohci_priv(h) \
	((struct nuc990_ohci_priv *)hcd_to_ohci(h)->priv)

static struct hc_driver __read_mostly ohci_nuc990_hc_driver;

static const struct ohci_driver_overrides ohci_nuc990_overrides __initconst = {
	.product_desc = "Nuvoton NUC990 OHCI",
	.extra_priv_size = sizeof(struct nuc990_ohci_priv),
};

/* ------------------------------------------------------------------------- */

static int nuc990_ohci_probe(struct platform_device *pdev)
{
	const struct hc_driver *driver = &ohci_nuc990_hc_driver;
	struct nuc990_ohci_priv *priv;
	struct usb_hcd *hcd;
	struct resource *res;
	int irq;
	int ret;

	if (usb_disabled())
		return -ENODEV;

	dev_info(&pdev->dev, "Initializing NUC990 OHCI\n");

	irq = platform_get_irq(pdev, 0);
	if (irq < 0)
		return irq;

	ret = dma_coerce_mask_and_coherent(&pdev->dev, DMA_BIT_MASK(32));
	if (ret)
		return ret;

	hcd = usb_create_hcd(driver, &pdev->dev, dev_name(&pdev->dev));
	if (!hcd)
		return -ENOMEM;

	platform_set_drvdata(pdev, hcd);

	res = platform_get_resource(pdev, IORESOURCE_MEM, 0);
	hcd->regs = devm_ioremap_resource(&pdev->dev, res);
	if (IS_ERR(hcd->regs)) {
		ret = PTR_ERR(hcd->regs);
		goto err_put_hcd;
	}

	hcd->rsrc_start = res->start;
	hcd->rsrc_len   = resource_size(res);

	priv = hcd_to_nuc990_ohci_priv(hcd);

	priv->clk = of_clk_get(pdev->dev.of_node, 0);
	if (IS_ERR(priv->clk)) {
		ret = PTR_ERR(priv->clk);
		dev_err(&pdev->dev, "failed to get clock: %d\n", ret);
		goto err_put_hcd;
	}

	ret = clk_prepare_enable(priv->clk);
	if (ret) {
		dev_err(&pdev->dev, "failed to enable clock: %d\n", ret);
		goto err_put_clk;
	}

	ret = usb_add_hcd(hcd, irq, IRQF_SHARED);
	if (ret)
		goto err_disable_clk;

	device_wakeup_enable(hcd->self.controller);

	return 0;

err_disable_clk:
	clk_disable_unprepare(priv->clk);
err_put_clk:
	clk_put(priv->clk);
err_put_hcd:
	usb_put_hcd(hcd);
	return ret;
}

static int nuc990_ohci_remove(struct platform_device *pdev)
{
	struct usb_hcd *hcd = platform_get_drvdata(pdev);
	struct nuc990_ohci_priv *priv = hcd_to_nuc990_ohci_priv(hcd);

	usb_remove_hcd(hcd);

	clk_disable_unprepare(priv->clk);
	clk_put(priv->clk);

	usb_put_hcd(hcd);

	dev_dbg(&pdev->dev, "NUC990 OHCI removed\n");

	return 0;
}

static int __maybe_unused nuc990_ohci_suspend(struct device *dev)
{
	struct usb_hcd *hcd = dev_get_drvdata(dev);
	struct ohci_hcd *ohci = hcd_to_ohci(hcd);
	struct nuc990_ohci_priv *priv = hcd_to_nuc990_ohci_priv(hcd);
	bool do_wakeup = device_may_wakeup(dev);
	int ret;

	if (!ohci)
		return -EINVAL;

	if (time_before(jiffies, ohci->next_statechange))
		msleep(5);
	ohci->next_statechange = jiffies;

	ret = ohci_suspend(hcd, do_wakeup);
	if (ret)
		return ret;

	clk_disable_unprepare(priv->clk);

	return 0;
}

static int __maybe_unused nuc990_ohci_resume(struct device *dev)
{
	struct usb_hcd *hcd = dev_get_drvdata(dev);
	struct nuc990_ohci_priv *priv = hcd_to_nuc990_ohci_priv(hcd);
	struct ohci_hcd *ohci = hcd_to_ohci(hcd);
	int ret;

	if (!ohci)
		return -EINVAL;

	if (time_before(jiffies, ohci->next_statechange))
		msleep(5);
	ohci->next_statechange = jiffies;

	ret = clk_prepare_enable(priv->clk);
	if (ret)
		return ret;

	ohci_resume(hcd, false);
	return 0;
}

static SIMPLE_DEV_PM_OPS(nuc990_ohci_pm_ops, nuc990_ohci_suspend, nuc990_ohci_resume);

static const struct of_device_id nuc990_ohci_dt_ids[] = {
	{ .compatible = "nuvoton,nuc990-ohci" },
	{ /* sentinel */ }
};
MODULE_DEVICE_TABLE(of, nuc990_ohci_dt_ids);

static struct platform_driver nuc990_ohci_driver = {
	.probe		= nuc990_ohci_probe,
	.remove		= nuc990_ohci_remove,
	.shutdown	= usb_hcd_platform_shutdown,
	.driver		= {
		.name		= "nuc990-ohci",
		.pm		= &nuc990_ohci_pm_ops,
		.of_match_table	= nuc990_ohci_dt_ids,
	},
};

static int __init nuc990_ohci_init(void)
{
	if (usb_disabled())
		return -ENODEV;

	pr_info("%s: %s\n", hcd_name, DRIVER_DESC);

	ohci_init_driver(&ohci_nuc990_hc_driver, &ohci_nuc990_overrides);

	return platform_driver_register(&nuc990_ohci_driver);
}
module_init(nuc990_ohci_init);

static void __exit nuc990_ohci_exit(void)
{
	platform_driver_unregister(&nuc990_ohci_driver);
}
module_exit(nuc990_ohci_exit);

MODULE_DESCRIPTION(DRIVER_DESC);
MODULE_ALIAS("platform:nuc990-ohci");
MODULE_LICENSE("GPL v2");
