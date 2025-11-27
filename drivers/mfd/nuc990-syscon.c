// SPDX-License-Identifier: GPL-2.0
/*
 * Nuvoton NUC990 System Controller + Reset Controller
 *
 * Copyright (C) 2025 Nuvoton Technology Corp.
 *
 * Author: Joey Lu <yclu4@nuvoton.com>
 */

#include <linux/module.h>
#include <linux/platform_device.h>
#include <linux/of_device.h>
#include <linux/reboot.h>
#include <linux/regmap.h>
#include <linux/mfd/syscon.h>
#include <linux/reset-controller.h>
#include <dt-bindings/reset/nuvoton,nuc990-reset.h>

#define NUC990_SYS_RST_OFFSET(x)	(0x60 + ((x % 32) * 4))
#define NUC990_SYS_RST_MASK(x)		BIT((x) % 32)
#define NUC990_SYS_REGLKCTL			0x1A0

struct nuc990_syscon {
	struct regmap	*map;
	spinlock_t		lock;
	int				rec;
	struct reset_controller_dev	rcdev;
	struct notifier_block		restart_handler;
};

struct nuc990_syscon *nuc990_sysc;

void nuc990_reg_unlock(void)
{
	unsigned long flags;
	unsigned int reg;

	if(!nuc990_sysc) {
		pr_warn("nuc990_sysc is NULL in nuc990_reg_unlock\n");
		return;
	}

	spin_lock_irqsave(&nuc990_sysc->lock, flags);

	nuc990_sysc->rec++;
	while (1) {
		regmap_read(nuc990_sysc->map, NUC990_SYS_REGLKCTL, &reg);
		if (reg == 1)
			break;

		regmap_write(nuc990_sysc->map, NUC990_SYS_REGLKCTL, 0x59);
		regmap_write(nuc990_sysc->map, NUC990_SYS_REGLKCTL, 0x16);
		regmap_write(nuc990_sysc->map, NUC990_SYS_REGLKCTL, 0x88);
	}

	spin_unlock_irqrestore(&nuc990_sysc->lock, flags);
}
EXPORT_SYMBOL(nuc990_reg_unlock);

void nuc990_reg_lock(void)
{
	unsigned long flags;

	if(!nuc990_sysc) {
		pr_warn("nuc990_sysc is NULL in nuc990_reg_lock\n");
		return;
	}

	spin_lock_irqsave(&nuc990_sysc->lock, flags);
	if (--nuc990_sysc->rec == 0)
		regmap_write(nuc990_sysc->map, NUC990_SYS_REGLKCTL, 1);

	spin_unlock_irqrestore(&nuc990_sysc->lock, flags);
}
EXPORT_SYMBOL(nuc990_reg_lock);

static int nuc990_reset_assert(struct reset_controller_dev *rcdev,
			       unsigned long id)
{
	struct nuc990_syscon *rst = container_of(rcdev, struct nuc990_syscon, rcdev);

	nuc990_reg_unlock();
	regmap_update_bits(rst->map, NUC990_SYS_RST_OFFSET(id),
			  NUC990_SYS_RST_MASK(id), NUC990_SYS_RST_MASK(id));
	nuc990_reg_lock();

	return 0;
}

static int nuc990_reset_deassert(struct reset_controller_dev *rcdev,
				 unsigned long id)
{
	struct nuc990_syscon *rst = container_of(rcdev, struct nuc990_syscon, rcdev);

	nuc990_reg_unlock();
	regmap_update_bits(rst->map, NUC990_SYS_RST_OFFSET(id),
			  NUC990_SYS_RST_MASK(id), 0);
	nuc990_reg_lock();

	return 0;
}

static int nuc990_reset_status(struct reset_controller_dev *rcdev,
			       unsigned long id)
{
	struct nuc990_syscon *rst = container_of(rcdev, struct nuc990_syscon, rcdev);
	unsigned int val;

	regmap_read(rst->map, NUC990_SYS_RST_OFFSET(id), &val);

	return !!(val & NUC990_SYS_RST_MASK(id));
}

static const struct reset_control_ops nuc990_reset_ops = {
	.assert   = nuc990_reset_assert,
	.deassert = nuc990_reset_deassert,
	.status   = nuc990_reset_status,
};

static int nuc990_restart_handler(struct notifier_block *nb,
				  unsigned long mode, void *cmd)
{
	struct nuc990_syscon *rst = container_of(nb, struct nuc990_syscon, restart_handler);

	nuc990_reg_unlock();
	regmap_update_bits(rst->map, NUC990_SYS_RST_OFFSET(NUC990_RESET_CHIP),
			  NUC990_SYS_RST_MASK(NUC990_RESET_CHIP), NUC990_SYS_RST_MASK(NUC990_RESET_CHIP));
	nuc990_reg_lock();

	return NOTIFY_DONE;
}

static int nuc990_syscon_probe(struct platform_device *pdev)
{
	struct device *dev = &pdev->dev;
	struct device_node *np = dev->of_node;
	struct nuc990_syscon *sys, *rst;
	struct regmap *map;
	int ret;

	if (of_device_is_compatible(np, "nuvoton,nuc990-syscon")) {
		/* Syscon root node */
		map = syscon_node_to_regmap(np);
		if (IS_ERR(map))
			return PTR_ERR(map);

		sys = devm_kzalloc(&pdev->dev, sizeof(*sys), GFP_KERNEL);
		if (!sys)
			return -ENOMEM;

		sys->map = map;
		spin_lock_init(&sys->lock);
		sys->rec = 0;
		nuc990_sysc = sys;

		dev_info(dev, "Registered syscon provider\n");

		return 0;
	}

	if (of_device_is_compatible(np, "nuvoton,nuc990-reset")) {
		/* Reset controller node */
		map = syscon_node_to_regmap(np->parent);
		if (IS_ERR(map))
			return PTR_ERR(map);

		rst = devm_kzalloc(&pdev->dev, sizeof(*rst), GFP_KERNEL);
		if (!rst)
			return -ENOMEM;

		rst->map = map;
		rst->rcdev.ops = &nuc990_reset_ops;
		rst->rcdev.owner = THIS_MODULE;
		rst->rcdev.nr_resets = NUC990_RST_NR;
		rst->rcdev.of_node = pdev->dev.of_node;

		rst->restart_handler.notifier_call = nuc990_restart_handler;
		rst->restart_handler.priority = 192;

		ret = register_restart_handler(&rst->restart_handler);
		if (ret) {
			dev_err(&pdev->dev, "Failed to register restart handler\n");
			return ret;
		}

		platform_set_drvdata(pdev, rst);
		return devm_reset_controller_register(&pdev->dev, &rst->rcdev);
	}

	return -ENODEV;
}

static const struct of_device_id nuc990_syscon_dt_ids[] = {
	{ .compatible = "nuvoton,nuc990-reset", },
	{ .compatible = "nuvoton,nuc990-syscon", },
	{ /* sentinel */ }
};
MODULE_DEVICE_TABLE(of, nuc990_syscon_dt_ids);

static struct platform_driver nuc990_syscon_driver = {
	.driver = {
		.name = "nuc990-syscon",
		.of_match_table = nuc990_syscon_dt_ids,
	},
	.probe = nuc990_syscon_probe,
};
module_platform_driver(nuc990_syscon_driver);

MODULE_AUTHOR("Joey Lu <yclu4@nuvoton.com>");
MODULE_DESCRIPTION("Nuvoton NUC990 Syscon + Reset Controller");
MODULE_LICENSE("GPL v2");
