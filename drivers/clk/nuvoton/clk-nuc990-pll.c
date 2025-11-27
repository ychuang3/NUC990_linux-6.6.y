// SPDX-License-Identifier: GPL-2.0
/*
 * Nuvoton NUC990 Clock Controller driver
 *
 * Copyright (C) 2025 Nuvoton Technology Corp.
 *
 * Author: Joey Lu <yclu4@nuvoton.com>
 */

#include <linux/clk.h>
#include <linux/clk-provider.h>
#include <linux/io.h>
#include <linux/slab.h>
#include <linux/bitfield.h>
#include <linux/math.h>

#include "clk-nuc990.h"
#include <dt-bindings/clock/nuvoton,nuc990-clk.h>

#define to_nuc990_clk_pll(p) \
	(container_of(p, struct nuc990_clk_pll, hw))

struct nuc990_clk_pll {
	struct clk_hw hw;
	struct regmap *base;
	unsigned int reg;
	unsigned long parent_rate;
	unsigned long rounded_rate;
	u32 n_div;
	u32 m_div;
	u32 p_div;
};

/**
 * Fout = Fin * N / (M * P)
 * 1 < N < 128
 * 1 < M < 64
 * 1 < P < 8
 * 
 * Fvco = Fin * N / M
 * 200MHz < Fvco < 500MHz
 * 
 * Fpfd = Fin / M
 */
struct pll_rate_bound {
	u8 max_n;
	u8 min_n;
	u8 max_m;
	u8 min_m;
	u8 max_p;
	u8 min_p;
};

struct pll_rate_table {
	struct pll_rate_bound pll;
	u16 max_vco;
	u16 min_vco;
	u16 max_pfd;
	u16 min_pfd; // double of min_pfd
};

struct pll_rate_table const nuc990_pll_rates[] = {
	{ .pll = { 1, 1, 64, 1, 8, 1 }, .max_vco = 500, .min_vco = 200,
	  .max_pfd = 80, .min_pfd = 22},
	{ .pll = { 2, 2, 64, 1, 8, 1 }, .max_vco = 500, .min_vco = 200,
	  .max_pfd = 80, .min_pfd = 14},
	{ .pll = { 3, 3, 64, 1, 8, 1 }, .max_vco = 500, .min_vco = 200,
	  .max_pfd = 80, .min_pfd = 10},
	{ .pll = { 4, 4, 64, 1, 8, 1 }, .max_vco = 500, .min_vco = 200,
	  .max_pfd = 80, .min_pfd = 8},
	{ .pll = { 5, 5, 64, 1, 8, 1 }, .max_vco = 500, .min_vco = 200,
	  .max_pfd = 80, .min_pfd = 7},
	{ .pll = { 6, 6, 64, 1, 8, 1 }, .max_vco = 500, .min_vco = 200,
	  .max_pfd = 80, .min_pfd = 6},
	{ .pll = { 8, 7, 64, 1, 8, 1 }, .max_vco = 500, .min_vco = 200,
	  .max_pfd = 80, .min_pfd = 5},
	{ .pll = { 10, 9, 64, 1, 8, 1 }, .max_vco = 500, .min_vco = 200,
	  .max_pfd = 80, .min_pfd = 7},
	{ .pll = { 40, 11, 64, 1, 8, 1 }, .max_vco = 500, .min_vco = 200,
	  .max_pfd = 80, .min_pfd = 6},
	{ .pll = { 128, 41, 64, 1, 8, 1 }, .max_vco = 500, .min_vco = 200,
	  .max_pfd = 80, .min_pfd = 5},
	{ /* sentinel */ }
};

static int nuc990_pll_calc(unsigned long rate, unsigned long parent_rate,
			   unsigned long *best_rate,
			   u32 *best_n, u32 *best_m, u32 *best_p)
{
	const struct pll_rate_table *ent;
	u32 n, m, p;
	u64 fout, vco, pfd, diff = ~0;
	int found = 0;

	*best_rate = 0;
	*best_n = 0;
	*best_m = 0;
	*best_p = 0;

	for (ent = nuc990_pll_rates; ent->pll.max_n != 0; ent++) {
		if (DIV_ROUND_UP(rate, parent_rate) > ent->pll.max_n)
			continue;

		for (p = ent->pll.min_p; p <= ent->pll.max_p; p++) {
			vco = rate * p;
			if (vco < ent->min_vco * 1000000ULL ||
			    vco > ent->max_vco * 1000000ULL)
				continue;

			for (m = ent->pll.min_m; m <= ent->pll.max_m; m++) {
				pfd = parent_rate / m;
				if (pfd < ent->min_pfd * 1000000ULL / 2 ||
				    pfd > ent->max_pfd * 1000000ULL)
					continue;

				n = DIV_ROUND_CLOSEST_ULL(vco * m, parent_rate);
				if (n < ent->pll.min_n || n > ent->pll.max_n)
					continue;

				fout = parent_rate * n / (m * p);
				if (fout == rate) {
					*best_rate = (unsigned long)fout;
					*best_n = n;
					*best_m = m;
					*best_p = p;
					return 0;
				}

				if (!found || abs_diff((unsigned long)fout, rate) < diff) {
					found = 1;
					*best_rate = (unsigned long)fout;
					*best_n = n;
					*best_m = m;
					*best_p = p;
					diff = abs_diff((unsigned long)fout, rate);
				}
			}
		}
	}

	return found ? 0 : -EINVAL;
}

static int nuc990_clk_pll_prepare(struct clk_hw *hw)
{
	struct nuc990_clk_pll *pll = to_nuc990_clk_pll(hw);
	u32 reg, val;

	reg = pll->reg;

	regmap_read(pll->base, reg, &val);
	val &= ~NUC990_PLLCON_PD;
	regmap_write(pll->base, reg, val);

	return 0;
}

static void nuc990_clk_pll_unprepare(struct clk_hw *hw)
{
	struct nuc990_clk_pll *pll = to_nuc990_clk_pll(hw);
	u32 reg, val;

	reg = pll->reg;
	regmap_read(pll->base, reg, &val);
	val |= NUC990_PLLCON_PD;
	regmap_write(pll->base, reg, val);
}

static int nuc990_clk_pll_is_prepared(struct clk_hw *hw)
{
	struct nuc990_clk_pll *pll = to_nuc990_clk_pll(hw);
	u32 reg, val;

	reg = pll->reg;
	regmap_read(pll->base, reg, &val);

	return (val & NUC990_PLLCON_STB) == NUC990_PLLCON_STB;
}

static unsigned long nuc990_clk_pll_recalc_rate(struct clk_hw *hw,
					  unsigned long parent_rate)
{
	struct nuc990_clk_pll *pll = to_nuc990_clk_pll(hw);
	unsigned long actual_rate;
	u32 n, m, p;
	u32 reg;

	reg = pll->reg;
	regmap_read(pll->base, reg, &n);
	n &= NUC990_PLLCON_N_MASK;
	regmap_read(pll->base, reg, &m);
	m &= NUC990_PLLCON_M_MASK;
	regmap_read(pll->base, reg, &p);
	p &= NUC990_PLLCON_P_MASK;
	actual_rate = pll->parent_rate * (n + 1) /\
		((m >> NUC990_PLLCON_M_OFFSET) + 1) / ((p >> NUC990_PLLCON_P_OFFSET) + 1);

	return actual_rate;
}

static long nuc990_clk_pll_round_rate(struct clk_hw *hw, unsigned long rate,
				unsigned long *parent_rate)
{
	struct nuc990_clk_pll *pll = to_nuc990_clk_pll(hw);
	int ret;
	
	ret = nuc990_pll_calc(rate, pll->parent_rate,
		&pll->rounded_rate, &pll->n_div, &pll->m_div, &pll->p_div);
	if (ret) {
		pr_debug("%s: %s rate %ld Invalid\n", __func__,
			 __clk_get_name(hw->clk), rate);
		return 0;
	}
	pr_debug("%s: %s new rate %ld [N=%u] [M=%u] [P=%u]\n",
		 __func__, __clk_get_name(hw->clk),
		 pll->rounded_rate, pll->n_div, pll->m_div, pll->p_div);

	return pll->rounded_rate;
}

static int nuc990_clk_pll_set_rate(struct clk_hw *hw, unsigned long rate,
				unsigned long parent_rate)
{
	struct nuc990_clk_pll *pll = to_nuc990_clk_pll(hw);
	u32 reg, val = 0;

	if (pll->rounded_rate != rate) {
		pr_debug("%s: %s rate %ld rounded to %ld\n", __func__,
			 __clk_get_name(hw->clk), rate, pll->rounded_rate);
	}

	if (pll->n_div == 0 || pll->m_div == 0 || pll->p_div == 0) {
		pr_err("%s: %s PLL not configured\n", __func__,
			__clk_get_name(hw->clk));
		return -EINVAL;
	}

	val |= FIELD_PREP(NUC990_PLLCON_N_MASK, pll->n_div - 1);
	val |= FIELD_PREP(NUC990_PLLCON_M_MASK, pll->m_div - 1);
	val |= FIELD_PREP(NUC990_PLLCON_P_MASK, pll->p_div - 1);
	reg = pll->reg;

	return regmap_update_bits(pll->base, reg,
		NUC990_PLLCON_N_MASK | NUC990_PLLCON_M_MASK | NUC990_PLLCON_P_MASK, val);
}

static const struct clk_ops nuc990_pll_ops = {
	.prepare = nuc990_clk_pll_prepare,
	.unprepare = nuc990_clk_pll_unprepare,
	.is_prepared = nuc990_clk_pll_is_prepared,
	.recalc_rate = nuc990_clk_pll_recalc_rate,
	.round_rate = nuc990_clk_pll_round_rate,
	.set_rate = nuc990_clk_pll_set_rate,
};

struct clk_hw *nuc990_reg_clk_pll(const char *name,
				 const char *parent, struct regmap *base,
				 enum nuc990_pll_type type,
				 unsigned long parent_rate)
{
    struct nuc990_clk_pll *pll;
    struct clk_hw *hw;
	struct clk_init_data init;
    int ret;

	pll = kmalloc(sizeof(*pll), GFP_KERNEL);
	if (!pll)
		return ERR_PTR(-ENOMEM);

	init.name = name;
	init.flags = 0;
	init.parent_names = &parent;
	init.num_parents = 1;
	init.ops = &nuc990_pll_ops;
	
	pll->hw.init = &init;
    pll->base = base;
	pll->reg = (type == NUC990_APLL) ? REG_CLK_APLLCON : REG_CLK_UPLLCON;
	pll->parent_rate = parent_rate;
	
	hw = &pll->hw;
	ret = clk_hw_register(NULL, hw);
	if (ret) {
		pr_err("failed to register pll clock!!!\n");
		kfree(pll);
		return ERR_PTR(ret);
	}

	return hw;
}
