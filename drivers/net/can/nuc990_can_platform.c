// SPDX-License-Identifier: GPL-2.0-or-later
/*
 *  NUC990 CAN driver
 *
 *  Copyright (C) 2025 Nuvoton Technology Corp.
 */

#include <linux/kernel.h>
#include <linux/module.h>
#include <linux/interrupt.h>
#include <linux/delay.h>
#include <linux/netdevice.h>
#include <linux/if_arp.h>
#include <linux/io.h>
#include <linux/platform_device.h>
#include <linux/clk.h>
#include <linux/of.h>
#include <linux/of_device.h>
#include <linux/of_irq.h>
#include <linux/pinctrl/consumer.h>

#include <linux/can/dev.h>

#include "nuc990_can.h"

#define REG_WUEN (0x168)

static u16 c_can_plat_read_reg_aligned_to_32bit(struct c_can_priv *priv,
						enum reg index)
{
	return readw(priv->base + 2 * priv->regs[index]);
}

static void c_can_plat_write_reg_aligned_to_32bit(struct c_can_priv *priv,
						  enum reg index, u16 val)
{
	writew(val, priv->base + 2 * priv->regs[index]);
}

static const struct of_device_id nuc990_can_of_table[] = {
	{
		.compatible = "nuvoton,nuc990-can",
	},
	{ /* sentinel */ },
};
MODULE_DEVICE_TABLE(of, nuc990_can_of_table);

static int c_can_plat_probe(struct platform_device *pdev)
{
	int ret;
	void __iomem *addr;
	struct net_device *dev;
	struct c_can_priv *priv;
	int irq;
	struct clk *clk;
	struct resource *mem;

	clk = devm_clk_get(&pdev->dev, NULL);
	if (IS_ERR(clk)) {
		dev_err(&pdev->dev, "failed to get clock\n");
		return PTR_ERR(clk);
	}
	ret = clk_prepare_enable(clk);
	if (ret) {
		dev_err(&pdev->dev, "failed to enable clock\n");
		return ret;
	}

	/* get the platform data */
	irq = platform_get_irq(pdev, 0);
	if (irq < 0) {
		ret = irq;
		goto exit;
	}

	addr = devm_platform_get_and_ioremap_resource(pdev, 0, &mem);
	if (IS_ERR(addr)) {
		ret = PTR_ERR(addr);
		goto exit;
	}

	/* allocate the c_can device */
	dev = alloc_c_can_dev();
	if (!dev) {
		ret = -ENOMEM;
		goto exit;
	}

	priv = netdev_priv(dev);

	priv->regs = reg_map_c_can;
	priv->read_reg = c_can_plat_read_reg_aligned_to_32bit;
	priv->write_reg = c_can_plat_write_reg_aligned_to_32bit;

	dev->irq = irq;
	priv->base = addr;
	priv->device = &pdev->dev;
	priv->can.clock.freq = clk_get_rate(clk);
	priv->clk = clk;
	priv->wakeup_enabled =
		of_property_read_bool(pdev->dev.of_node, "wakeup-source");

	platform_set_drvdata(pdev, dev);
	SET_NETDEV_DEV(dev, &pdev->dev);

	ret = register_c_can_dev(dev);
	if (ret) {
		dev_err(&pdev->dev, "registering %s failed (err=%d)\n",
			KBUILD_MODNAME, ret);
		goto exit_free_device;
	}

	dev_info(&pdev->dev, "%s device registered\n", KBUILD_MODNAME);
	return 0;

exit_free_device:
	free_c_can_dev(dev);
exit:
	clk_disable_unprepare(clk);
	dev_err(&pdev->dev, "probe failed\n");
	return ret;
}

static void c_can_plat_remove(struct platform_device *pdev)
{
	struct net_device *dev = platform_get_drvdata(pdev);
	struct c_can_priv *priv = netdev_priv(dev);

	unregister_c_can_dev(dev);
	clk_disable_unprepare(priv->clk);
	free_c_can_dev(dev);
}

#ifdef CONFIG_PM
static int c_can_suspend(struct device *dev)
{
	int ret;
	struct net_device *ndev = dev_get_drvdata(dev);
	struct c_can_priv *priv = netdev_priv(ndev);

	if (priv->wakeup_enabled) {
		__raw_writel(0x1, priv->base + REG_WUEN);
		ret = enable_irq_wake(ndev->irq);
		if (ret)
			dev_warn(dev, "failed to enable irq wake\n");
	}

	if (netif_running(ndev)) {
		netif_stop_queue(ndev);
		netif_device_detach(ndev);
	}

	ret = c_can_power_down(ndev);
	if (ret) {
		netdev_err(ndev, "failed to enter power down mode\n");
		return ret;
	}

	priv->can.state = CAN_STATE_SLEEPING;

	return 0;
}

static int c_can_resume(struct device *dev)
{
	struct net_device *ndev = dev_get_drvdata(dev);
	struct c_can_priv *priv = netdev_priv(ndev);
	int ret;

	if (priv->wakeup_enabled)
		disable_irq_wake(ndev->irq);

	ret = c_can_power_up(ndev);
	if (ret) {
		netdev_err(ndev, "Still in power down mode\n");
		return ret;
	}

	priv->can.state = CAN_STATE_ERROR_ACTIVE;

	if (netif_running(ndev)) {
		netif_device_attach(ndev);
		netif_start_queue(ndev);
	}

	return 0;
}
#else
#define c_can_suspend NULL
#define c_can_resume NULL
#endif

static const struct dev_pm_ops nuc990_can_pm_ops = {
	.suspend = c_can_suspend,
	.resume = c_can_resume,
};

static struct platform_driver nuc990_can_driver = {
	.driver = {
		.name = "nuc990-can",
		.owner = THIS_MODULE,
		.of_match_table = of_match_ptr(nuc990_can_of_table),
		.pm = &nuc990_can_pm_ops,
	},
	.probe    = c_can_plat_probe,
	.remove_new   = c_can_plat_remove,
};

module_platform_driver(nuc990_can_driver);

MODULE_AUTHOR("nuvoton");
MODULE_LICENSE("GPL");
MODULE_DESCRIPTION("Platform CAN bus driver for NUC990 controller");
