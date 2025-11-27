// SPDX-License-Identifier: GPL-2.0
/*
 * Nuvoton NUC990 irq driver
 *
 * Copyright (C) 2025 Nuvoton Technology Corp.
 *
 * Author: Joey Lu <yclu4@nuvoton.com>
 */

#include <linux/clk.h>
#include <linux/io.h>
#include <linux/irqchip.h>
#include <linux/irqdomain.h>
#include <linux/irqchip/chained_irq.h>
#include <linux/of_address.h>
#include <linux/of_irq.h>
#include <linux/of_platform.h>
#include <linux/slab.h>
#include <asm/exception.h>

#define NUC990_AIC_SRC_OFFSET(x)	((x) / 4 * 4)
#define NUC990_AIC_SRC_TT_SHIFT(x)	(((x) % 4) * 8 + 6)
#define NUC990_AIC_SRC_TT_VALID		(0x03)
#define NUC990_AIC_SRC_TT_LL		(0x00)
#define NUC990_AIC_SRC_TT_HL		(0x01)
#define NUC990_AIC_SRC_TT_FE		(0x02)
#define NUC990_AIC_SRC_TT_RE		(0x03)
#define NUC990_AIC_SRC_PL_SHIFT(x)	(((x) % 4) * 8)
#define NUC990_AIC_SRC_PL_VALID		(0x07)

#define NUC990_AIC_RAW_OFFSET(x)	(0x100 + ((x) / 32 * 4))
#define NUC990_AIC_IS_OFFSET(x)		(0x110 + ((x) / 32 * 4))
#define NUC990_AIC_IRQ				(0x120)
#define NUC990_AIC_IRQMASK			GENMASK(6, 0)
#define NUC990_AIC_FIQ				(0x124)
#define NUC990_AIC_IE_OFFSET(x)		(0x130 + ((x) / 32 * 4))
#define NUC990_AIC_IEN_OFFSET(x)	(0x140 + ((x) / 32 * 4))
#define NUC990_AIC_IDIS_OFFSET(x)	(0x150 + ((x) / 32 * 4))
#define NUC990_AIC_IRQ_BIT(x)		BIT((x) % 32)
#define NUC990_AIC_IRQRST			(0x170)
#define NUC990_AIC_FIQRST			(0x174)
#define NUC990_AIC_ACK				(0x01)

#define NUC990_AIC_IRQ_MAX			83

struct nuc990_irq_chip {
	void __iomem *base;
	struct irq_domain *domain;
	struct irq_chip *chip;
};

static struct nuc990_irq_chip *nuc990_ic;
static void __exception_irq_entry nuc990_irq_entry(struct pt_regs *regs)
{
	struct nuc990_irq_chip *priv = nuc990_ic;
	u32 hwirq = readl(priv->base + NUC990_AIC_IRQ) & NUC990_AIC_IRQMASK;

	/* invoke our AIC handler */
	generic_handle_domain_irq(priv->domain, hwirq);

	writel(NUC990_AIC_ACK, priv->base + NUC990_AIC_IRQRST);
}

static int nuc990_irq_domain_map(struct irq_domain *id, unsigned int virq,
				  irq_hw_number_t hw)
{
	struct nuc990_irq_chip *priv = id->host_data;

	irq_set_chip_data(virq, priv);
	irq_set_chip_and_handler(virq, priv->chip, handle_level_irq);
	irq_set_status_flags(virq, IRQ_LEVEL);
	irq_set_noprobe(virq);

	return 0;
}

static void nuc990_irq_domain_unmap(struct irq_domain *id, unsigned int virq)
{
	irq_set_chip_data(virq, NULL);
}

static const struct irq_domain_ops nuc990_irq_domain_ops = {
	.map    = nuc990_irq_domain_map,
	.unmap	= nuc990_irq_domain_unmap,
	.xlate  = irq_domain_xlate_twocell,
};

static void nuc990_irq_mask(struct irq_data *data)
{
	struct nuc990_irq_chip *priv = irq_data_get_irq_chip_data(data);
	u32 hwirq = data->hwirq;

	writel(BIT(hwirq % 32), priv->base + NUC990_AIC_IDIS_OFFSET(hwirq));
}

static void nuc990_irq_unmask(struct irq_data *data)
{
	struct nuc990_irq_chip *priv = irq_data_get_irq_chip_data(data);
	u32 hwirq = data->hwirq;

	writel(BIT(hwirq % 32), priv->base + NUC990_AIC_IEN_OFFSET(hwirq));
}

static void nuc990_irq_ack(struct irq_data *data)
{
	struct nuc990_irq_chip *priv = irq_data_get_irq_chip_data(data);

	writel(NUC990_AIC_ACK, priv->base + NUC990_AIC_IRQRST);
}

static int nuc990_irq_set_type(struct irq_data *data, unsigned int flow_type)
{
	struct nuc990_irq_chip *priv = irq_data_get_irq_chip_data(data);
	u32 hwirq = data->hwirq;
	u32 reg = NUC990_AIC_SRC_OFFSET(hwirq);
	u32 tt = NUC990_AIC_SRC_TT_SHIFT(hwirq);
	u32 val;

	val = readl(priv->base + reg);

	val &= ~((NUC990_AIC_SRC_TT_VALID << tt));

	switch (flow_type) {
	case IRQ_TYPE_EDGE_RISING:
		val |= NUC990_AIC_SRC_TT_RE << tt;
		break;
	case IRQ_TYPE_EDGE_FALLING:
		val |= NUC990_AIC_SRC_TT_FE << tt;
		break;
	case IRQ_TYPE_LEVEL_HIGH:
		val |= NUC990_AIC_SRC_TT_HL << tt;
		break;
	case IRQ_TYPE_LEVEL_LOW:
		val |= NUC990_AIC_SRC_TT_LL << tt;
		break;
	default:
		return -EINVAL;
	}

	writel(val, priv->base + reg);

	return 0;
}

static struct irq_chip nuc990_irq_chip = {
	.name = "nuc990-aic",
	.irq_disable = nuc990_irq_mask,
	.irq_ack = nuc990_irq_ack,
	.irq_mask = nuc990_irq_mask,
	.irq_unmask = nuc990_irq_unmask,
	.irq_set_type = nuc990_irq_set_type,
};

static int __init nuc990_aic_init(struct device_node *node,
				struct device_node *parent)
{
	struct nuc990_irq_chip *priv;

	priv = kzalloc(sizeof(*priv), GFP_KERNEL);
	if (!priv)
		return -ENOMEM;

	priv->base = of_iomap(node, 0);
	if (!priv->base) {
		kfree(priv);
		return -ENOMEM;
	}

	nuc990_ic = priv;
	priv->chip = &nuc990_irq_chip;
	priv->domain = irq_domain_add_linear(node, NUC990_AIC_IRQ_MAX,
					&nuc990_irq_domain_ops, priv);
	if (!priv->domain) {
		iounmap(priv->base);
		kfree(priv);
		return -ENOMEM;
	}

	/* register the root IRQ handler */
	set_handle_irq(nuc990_irq_entry);

	return 0;
}
IRQCHIP_DECLARE(nuc990_aic, "nuvoton,nuc990-aic", nuc990_aic_init);

static int __init nuc990_xint_init(struct device_node *node,
				struct device_node *parent)
{
	// TODO: implement external interrupt initialization
	return 0;
}
IRQCHIP_DECLARE(nuc990_xint, "nuvoton,nuc990-xint", nuc990_xint_init);
