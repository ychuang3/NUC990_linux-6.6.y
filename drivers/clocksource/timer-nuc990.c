// SPDX-License-Identifier: GPL-2.0
/*
 * Nuvoton NUC990 Clocksource driver
 *
 * Copyright (C) 2025 Nuvoton Technology Corp.
 *
 * Author: Joey Lu <yclu4@nuvoton.com>
 */

#include <linux/bitfield.h>
#include <linux/clk.h>
#include <linux/clockchips.h>
#include <linux/clocksource.h>
#include <linux/delay.h>
#include <linux/interrupt.h>
#include <linux/irq.h>
#include <linux/kernel.h>
#include <linux/of.h>
#include <linux/of_address.h>
#include <linux/of_irq.h>
#include <linux/platform_device.h>
#include <linux/sched_clock.h>

#define NUC990_TMR_CTL		0x0
#define NUC990_TMR_CMP		0x4
#define NUC990_TMR_INTSTS	0x8
#define NUC990_TMR_CNT		0xC

#define NUC990_TMR_CTL_CNTEN	BIT(30)
#define NUC990_TMR_CTL_INTEN	BIT(29)
#define NUC990_TMR_CTL_OPMODE	GENMASK(28, 27)
#define OPMODE_ONESHOT		0
#define OPMODE_PERIODIC		1
#define OPMODE_TOGGLE		2
#define OPMODE_CONTINUOUS	3
#define NUC990_TMR_CTL_ACTSTS	BIT(25)
#define NUC990_TMR_CTL_PSC		GENMASK(7, 0)
#define NUC990_TMR_CMP_MASK		GENMASK(23, 0)
#define NUC990_TMR_INTSTS_TIF	BIT(0)
#define NUC990_TMR_CNT_MASK		GENMASK(23, 0)
#define NUC990_TMR_CNT_WIDTH	24

#define to_nuc990_clk_event(c) \
	(container_of(c, struct nuc990_clock_event, ce_dev))

struct nuc990_clock_event {
	struct clock_event_device ce_dev;
	void __iomem *base;
	unsigned long rate;
	int irq;
	u32 ticks_per_jiffy;
};

static struct delay_timer nuc990_delay_timer;
static void __iomem *clocksource_timer_counter;

static u64 notrace nuc990_read_sched_clock(void)
{
	return readl(clocksource_timer_counter);
}

static unsigned long nuc990_delay_timer_read(void)
{
	return readl(clocksource_timer_counter);
}

static int __init nuc990_clocksource_init(struct device_node *np)
{
	void __iomem *base;
	unsigned long rate;
	struct clk *clk;
	u32 reg, prescale;
	int ret;

	clk = of_clk_get(np, 0);
	if (IS_ERR(clk)) {
		pr_err("%pOF: failed to get clock\n", np);
		return PTR_ERR(clk);
	}

	ret = clk_prepare_enable(clk);
	if (ret) {
		pr_err("%pOF: failed to enable clock\n", np);
		goto err_clk_enable;
	}

	base = of_iomap(np, 0);
	if (!base) {
		pr_err("%pOF: failed to map registers\n", np);
		ret = -ENOMEM;
		goto err_iomap;
	}

	/* clock source in continuous mode */
	prescale = readl_relaxed(base + NUC990_TMR_CTL) & NUC990_TMR_CTL_PSC;
	reg = FIELD_PREP(NUC990_TMR_CTL_OPMODE, OPMODE_CONTINUOUS) | prescale;
	writel_relaxed((1ul << NUC990_TMR_CNT_WIDTH) - 1, base + NUC990_TMR_CMP);
	writel_relaxed(0, base + NUC990_TMR_INTSTS);
	writel_relaxed(reg | NUC990_TMR_CTL_CNTEN, base + NUC990_TMR_CTL);

	rate = DIV_ROUND_UP(clk_get_rate(clk), prescale + 1);
	ret = clocksource_mmio_init(base + NUC990_TMR_CNT, np->name,
				    rate, 200, NUC990_TMR_CNT_WIDTH,
				    clocksource_mmio_readl_up);
	if (ret) {
		pr_err("%pOF: failed to register clocksource\n", np);
		goto err_clocksource_init;
	}

	clocksource_timer_counter = base + NUC990_TMR_CNT;
	nuc990_delay_timer.read_current_timer = nuc990_delay_timer_read;
	nuc990_delay_timer.freq = rate;
	register_current_timer_delay(&nuc990_delay_timer);
	sched_clock_register(nuc990_read_sched_clock,
			NUC990_TMR_CNT_WIDTH, rate);

	pr_info("%pOF: NUC990 sched_clock registered\n", np);

	return 0;

err_clocksource_init:
	iounmap(base);
err_iomap:
	clk_disable_unprepare(clk);
err_clk_enable:
	clk_put(clk);
	return ret;
}

static irqreturn_t nuc990_clock_event_handler(int irq, void *dev_id)
{
	struct nuc990_clock_event *priv = dev_id;

	/* Clear interrupt flag */
	writel_relaxed(NUC990_TMR_INTSTS_TIF, priv->base + NUC990_TMR_INTSTS);

	priv->ce_dev.event_handler(&priv->ce_dev);

	return IRQ_HANDLED;
}

static void nuc990_timer_start(struct nuc990_clock_event *priv)
{
	u32 reg;

	reg = readl_relaxed(priv->base + NUC990_TMR_CTL);
	reg |= NUC990_TMR_CTL_CNTEN;
	writel_relaxed(reg, priv->base + NUC990_TMR_CTL);
}

static int nuc990_clkevt_next_event(unsigned long evt,
				 struct clock_event_device *dev)
{
	struct nuc990_clock_event *priv = to_nuc990_clk_event(dev);

	writel_relaxed(evt & NUC990_TMR_CMP_MASK, priv->base + NUC990_TMR_CMP);

	nuc990_timer_start(priv);

	return 0;
}

static int nuc990_clkevt_shutdown(struct clock_event_device *dev)
{
	struct nuc990_clock_event *priv = to_nuc990_clk_event(dev);
	u32 reg;

	reg = readl_relaxed(priv->base + NUC990_TMR_CTL);
	reg &= ~(NUC990_TMR_CTL_CNTEN | NUC990_TMR_CTL_INTEN);
	writel_relaxed(reg, priv->base + NUC990_TMR_CTL);

	return 0;
}

static int nuc990_clkevt_oneshot(struct clock_event_device *dev)
{
	struct nuc990_clock_event *priv = to_nuc990_clk_event(dev);
	u32 reg;

	reg = readl_relaxed(priv->base + NUC990_TMR_CTL) & NUC990_TMR_CTL_PSC;
	reg |= FIELD_PREP(NUC990_TMR_CTL_OPMODE, OPMODE_ONESHOT) |
			NUC990_TMR_CTL_INTEN;
	writel_relaxed(reg, priv->base + NUC990_TMR_CTL);

	return 0;
}

static int nuc990_clkevt_periodic(struct clock_event_device *dev)
{
	struct nuc990_clock_event *priv = to_nuc990_clk_event(dev);
	u32 reg;

	writel_relaxed(priv->ticks_per_jiffy & NUC990_TMR_CMP_MASK,
		       priv->base + NUC990_TMR_CMP);

	reg = readl_relaxed(priv->base + NUC990_TMR_CTL) & NUC990_TMR_CTL_PSC;
	reg |= FIELD_PREP(NUC990_TMR_CTL_OPMODE, OPMODE_PERIODIC) |
			NUC990_TMR_CTL_INTEN;
	writel_relaxed(reg, priv->base + NUC990_TMR_CTL);

	nuc990_timer_start(priv);

	return 0;
}

static struct nuc990_clock_event nuc990_clk_event = {
	.ce_dev = {
		.name		= "nuc990 clockevent",
		.features	= CLOCK_EVT_FEAT_ONESHOT | CLOCK_EVT_FEAT_PERIODIC,
		.rating		= 300,
		.set_next_event		= nuc990_clkevt_next_event,
		.set_state_shutdown	= nuc990_clkevt_shutdown,
		.set_state_oneshot	= nuc990_clkevt_oneshot,
		.set_state_periodic	= nuc990_clkevt_periodic,
	},
};

static int __init nuc990_clockevent_init(struct device_node *np)
{
	struct nuc990_clock_event *priv = &nuc990_clk_event;
	struct clk *clk;
	u32 prescale;
	int ret;

	clk = of_clk_get(np, 0);
	if (IS_ERR(clk)) {
		pr_err("%pOF: failed to get clock\n", np);
		return PTR_ERR(clk);
	}

	ret = clk_prepare_enable(clk);
	if (ret) {
		pr_err("%pOF: failed to enable clock\n", np);
		return ret;
	}

	priv->base = of_iomap(np, 0);
	if (!priv->base) {
		pr_err("%pOF: failed to map registers\n", np);
		return -ENOMEM;
	}

	priv->irq = irq_of_parse_and_map(np, 0);
	if (!priv->irq) {
		pr_err("%pOF: failed to get irq\n", np);
		return -EINVAL;
	}

	ret = request_irq(priv->irq, nuc990_clock_event_handler,
			  IRQF_TIMER | IRQF_IRQPOLL, "nuc990 clockevent", priv);
	if (ret) {
		pr_err("%pOF: request irq failed\n", np);
		return ret;
	}

	/* reset clock event */
	prescale = readl_relaxed(priv->base + NUC990_TMR_CTL) & NUC990_TMR_CTL_PSC;
	writel_relaxed(0, priv->base + NUC990_TMR_INTSTS);
	writel_relaxed(0, priv->base + NUC990_TMR_CNT);
	writel_relaxed(prescale, priv->base + NUC990_TMR_CTL);

	priv->rate = DIV_ROUND_UP(clk_get_rate(clk), prescale + 1);
	priv->ticks_per_jiffy = DIV_ROUND_CLOSEST(priv->rate, HZ);

	clockevents_config_and_register(&priv->ce_dev,
		priv->rate, 1, (1 << NUC990_TMR_CNT_WIDTH) - 1);

	pr_info("%pOF: NUC990 clockevent registered\n", np);

	return 0;
}

static int __init nuc990_timer_init(struct device_node *np)
{
	if (of_device_is_compatible(np, "nuvoton,nuc990-clksrc")) {
		return nuc990_clocksource_init(np);
	}

	if (of_device_is_compatible(np, "nuvoton,nuc990-clkevt")) {
		return nuc990_clockevent_init(np);
	}

	return 0;
}
TIMER_OF_DECLARE(nuc990_clksrc, "nuvoton,nuc990-clksrc", nuc990_timer_init);
TIMER_OF_DECLARE(nuc990_clkevt, "nuvoton,nuc990-clkevt", nuc990_timer_init);

