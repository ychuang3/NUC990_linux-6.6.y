// SPDX-License-Identifier: GPL-2.0
/*
 * Nuvoton NUC990 Clock Controller driver
 *
 * Copyright (C) 2025 Nuvoton Technology Corp.
 *
 * Author: Joey Lu <yclu4@nuvoton.com>
 */
#ifndef __MACH_NUC990_CLK_CCF_H
#define __MACH_NUC990_CLK_CCF_H

#include <linux/clk.h>
#include <linux/module.h>
#include <linux/kernel.h>
#include <linux/spinlock.h>
#include <linux/clk-provider.h>
#include <linux/regmap.h>

#define REG_CLK_PMCON		(0x000)	/*  Power Management Control Register */
#define REG_CLK_HCLKEN0		(0x010)	/*  AHB IP Clock Enable Control Register 0 */
#define REG_CLK_HCLKEN1		(0x014)	/*  AHB IP Clock Enable Control Register 1 */
#define REG_CLK_PCLKEN0		(0x018)	/*  APB IP Clock Enable Control Register 0 */
#define REG_CLK_PCLKEN1		(0x01C)	/*  APB IP Clock Enable Control Register 1 */
#define REG_CLK_DIV0		(0x020)	/*  Clock Divider Control Register 0 */
#define REG_CLK_DIV1		(0x024)	/*  Clock Divider Control Register 1 */
#define REG_CLK_DIV2		(0x028)	/*  Clock Divider Control Register 2 */
#define REG_CLK_DIV3		(0x02C)	/*  Clock Divider Control Register 3 */
#define REG_CLK_DIV4		(0x030)	/*  Clock Divider Control Register 4 */
#define REG_CLK_DIV5		(0x034)	/*  Clock Divider Control Register 5 */
#define REG_CLK_DIV6		(0x038)	/*  Clock Divider Control Register 6 */
#define REG_CLK_DIV7		(0x03C)	/*  Clock Divider Control Register 7 */
#define REG_CLK_DIV8		(0x040)	/*  Clock Divider Control Register 8 */
#define REG_CLK_DIV9		(0x044)	/*  Clock Divider Control Register 9 */
#define REG_CLK_APLLCON		(0x060)	/*  APLL Control Register */
#define REG_CLK_UPLLCON		(0x064)	/*  UPLL Control Register */

#define NUC990_PMCON_HXTEN		BIT(0)

#define NUC990_PLLCON_STB		BIT(31)
#define NUC990_PLLCON_RST		BIT(30)
#define NUC990_PLLCON_BYP		BIT(29)
#define NUC990_PLLCON_PD		BIT(28)
#define NUC990_PLLCON_P_MASK	GENMASK(15, 13)
#define NUC990_PLLCON_P_OFFSET	13
#define NUC990_PLLCON_M_MASK	GENMASK(12, 7)
#define NUC990_PLLCON_M_OFFSET	7
#define NUC990_PLLCON_N_MASK	GENMASK(6, 0)

#define HXT_RATE_DEFAULT		12000000
#define LXT_RATE_DEFAULT		32768

enum nuc990_pll_type {
	NUC990_APLL,
	NUC990_UPLL,
};

struct clk_hw *nuc990_reg_clk_pll(const char *name,
				 const char *parent, struct regmap *base,
				 enum nuc990_pll_type type,
				 unsigned long parent_rate);

extern spinlock_t nuc990_lock;

static inline struct clk_hw *nuc990_clk_fixed(const char *name, int rate)
{
	return clk_hw_register_fixed_rate(NULL, name, NULL, 0, rate);
}

static inline struct clk_hw *nuc990_clk_mux(const char *name,
					 void __iomem *reg,
					 u8 shift,
					 u8 width, const char *const *parents,
					 int num_parents)
{
	return clk_hw_register_mux(NULL, name, parents, num_parents, 0, reg,
				shift, width, 0, &nuc990_lock);
}

static inline struct clk_hw *nuc990_clk_divider(const char *name,
					     const char *parent,
					     void __iomem *reg, u8 shift,
					     u8 width)
{
	return clk_hw_register_divider(NULL, name, parent, 0,
				    reg, shift, width, 0, &nuc990_lock);
}

static inline struct clk_hw *nuc990_clk_div_pow2(const char *name,
					     const char *parent,
					     void __iomem *reg, u8 shift,
					     u8 width)
{
	return clk_hw_register_divider(NULL, name, parent, 0,
				    reg, shift, width, CLK_DIVIDER_POWER_OF_TWO, &nuc990_lock);
}

static inline struct clk_hw *nuc990_clk_fixed_factor(const char *name,
						  const char *parent,
						  unsigned int mult,
						  unsigned int div)
{
	return clk_hw_register_fixed_factor(NULL, name, parent,
					 CLK_SET_RATE_PARENT, mult, div);
}

static inline struct clk_hw *nuc990_clk_gate(const char *name,
					  const char *parent,
					  void __iomem *reg, u8 shift)
{
	return clk_hw_register_gate(NULL, name, parent, CLK_SET_RATE_PARENT, reg,
				 shift, 0, &nuc990_lock);
}

static inline struct clk_hw *nuc990_clk_gate_critical(const char *name,
					  const char *parent,
					  void __iomem *reg, u8 shift)
{
	return clk_hw_register_gate(NULL, name, parent, CLK_IS_CRITICAL, reg,
				 shift, 0, &nuc990_lock);
}

#endif
