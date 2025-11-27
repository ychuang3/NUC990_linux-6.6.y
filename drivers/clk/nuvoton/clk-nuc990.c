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
#include <linux/clkdev.h>
#include <linux/io.h>
#include <linux/module.h>
#include <linux/of.h>
#include <linux/of_address.h>
#include <linux/platform_device.h>
#include <linux/spinlock.h>
#include <linux/pm.h>

#include "clk-nuc990.h"
#include <dt-bindings/clock/nuvoton,nuc990-clk.h>

DEFINE_SPINLOCK(nuc990_lock);
static struct clk_hw **hws;
static struct clk_hw_onecell_data *nuc990_hw_data;

static const char *const sys_sel_clks[] = {
	"hxt_gate", "dummy", "apll", "upll"
};

static const char *const pclk3_sel_clks[] = {
	"hxt_gate", "hclk_gate", "apll_div", "upll_div"
};

static const char *const clko_sel_clks[] = {
	"hxt_gate", "lxt", "apll", "upll"
};

static const char *const sdh0_sel_clks[] = {
	"hxt_gate", "dummy", "apll_div", "upll_div"
};

static const char *const sdh1_sel_clks[] = {
	"hxt_gate", "dummy", "apll_div", "upll_div"
};

static const char *const i2s_sel_clks[] = {
	"hxt_gate", "dummy", "apll", "upll_div"
};

static const char *const canfd_sel_clks[] = {
	"apll_div", "upll_div"
};

static const char *const adc0_sel_clks[] = {
	"hxt_gate", "dummy", "apll_div", "upll_div"
};

static const char *const tmr0_sel_clks[] = {
	"hxt_gate", "pclk0_div2", "pclk0_div4k", "lxt"
};

static const char *const tmr1_sel_clks[] = {
	"hxt_gate", "pclk0_div2", "pclk0_div4k", "lxt"
};

static const char *const tmr2_sel_clks[] = {
	"hxt_gate", "pclk1_div2", "pclk1_div4k", "lxt"
};

static const char *const tmr3_sel_clks[] = {
	"hxt_gate", "pclk1_div2", "pclk1_div4k", "lxt"
};

static const char *const tmr4_sel_clks[] = {
	"hxt_gate", "pclk0_div2", "pclk0_div4k", "lxt"
};

static const char *const tmr5_sel_clks[] = {
	"hxt_gate", "pclk0_div2", "pclk0_div4k", "lxt"
};

static const char *const qspi0_sel_clks[] = {
	"hxt_gate", "pclk0_gate", "apll_div", "upll_div"
};

static const char *const spi0_sel_clks[] = {
	"hxt_gate", "pclk0_gate", "apll_div", "upll_div"
};

static const char *const spi1_sel_clks[] = {
	"hxt_gate", "pclk1_gate", "apll_div", "upll_div"
};

static const char *const spi2_sel_clks[] = {
	"hxt_gate", "pclk0_gate", "apll_div", "upll_div"
};

static const char *const spi3_sel_clks[] = {
	"hxt_gate", "pclk1_gate", "apll_div", "upll_div"
};

static const char *const uart_sel_clks[] = {
	"hxt_gate", "lxt", "apll_div", "upll_div"
};

static const char *const wdt0_sel_clks[] = {
	"hxt_gate", "hxt_div512", "pclk2_div4k", "lxt"
};

static const char *const wwdt0_sel_clks[] = {
	"hxt_gate", "hxt_div512", "pclk2_div4k", "lxt"
};

static struct regmap_config nuc990_regmap_config = {
	.name = "nuc990-clk",
	.reg_bits = 32,
	.val_bits = 32,
	.reg_stride = 4,
	.val_format_endian = REGMAP_ENDIAN_LITTLE,
	.max_register = 0x100,
	.fast_io = true,
};

static void __iomem *clk_base;
static void nuc990_pm_power_off(void)
{
	u32 reg;

	if (IS_ERR(clk_base)) {
		pr_err("clk is NULL in nuc990_pm_power_off\n");
		return;
	}

	reg = readl(clk_base + REG_CLK_PMCON);
	writel(reg & ~NUC990_PMCON_HXTEN, clk_base + REG_CLK_PMCON);
}

static void __init nuc990_clk_init(struct device_node *np)
{
	void __iomem *base;
	struct regmap *regmap;
	struct clk *src;
	unsigned long rate;
	int ret;

	nuc990_hw_data = kzalloc(struct_size(nuc990_hw_data, hws, NUC990_CLK_IDX),
				 GFP_KERNEL);

	if (!nuc990_hw_data)
		return;

	nuc990_hw_data->num = NUC990_CLK_IDX;
	hws = nuc990_hw_data->hws;

	base = of_iomap(np, 0);
	if (!base) {
		pr_err("%s: could not map region\n", __func__);
		return;
	}

	regmap = regmap_init_mmio(NULL, base, &nuc990_regmap_config);
	if (IS_ERR(regmap)) {
		pr_err("%s: could not allocate regmap\n", __func__);
		iounmap(base);
		return;
	}

	/* Clock sources */
	src = of_clk_get_by_name(np, "hxt");
	if (IS_ERR(src)) {
		pr_warn("nuc990-clk: hxt not found in DT\n");
		rate = HXT_RATE_DEFAULT;
	} else {
		rate = clk_get_rate(src);
	}
	hws[HXT_GATE] =	nuc990_clk_gate("hxt_gate", "hxt", base + REG_CLK_PMCON, 0);
	hws[APLL] = nuc990_reg_clk_pll("apll", "hxt_gate", regmap, NUC990_APLL, rate);
	hws[UPLL] = nuc990_reg_clk_pll("upll", "hxt_gate", regmap, NUC990_UPLL, rate);
	src = of_clk_get_by_name(np, "lxt");
	if (IS_ERR(src))
		pr_warn("nuc990-clk: lxt not found in DT\n");

	hws[SYS_MUX] = nuc990_clk_mux("sys_mux", base + REG_CLK_DIV0, 3, 2, sys_sel_clks, ARRAY_SIZE(sys_sel_clks));
	hws[CPU_DIV] = nuc990_clk_divider("cpu_div", "sys_mux", base + REG_CLK_DIV0, 8, 1);
	hws[CPU_GATE] = nuc990_clk_gate_critical("cpu_gate", "cpu_div", base + REG_CLK_HCLKEN0, 0);
	hws[HCLK] = nuc990_clk_fixed_factor("hclk", "sys_mux", 1, 2);
	hws[HCLK_GATE] = nuc990_clk_gate_critical("hclk_gate", "hclk", base + REG_CLK_HCLKEN0, 1);
	hws[HCLK1] = nuc990_clk_fixed_factor("hclk1", "hclk_gate", 1, 2);
	hws[HCLK1_GATE] = nuc990_clk_gate_critical("hclk1_gate", "hclk1", base + REG_CLK_HCLKEN0, 2);
	hws[PCLK0_GATE] = nuc990_clk_gate("pclk0_gate", "hclk_gate", base + REG_CLK_HCLKEN0, 12);
	hws[PCLK1_GATE] = nuc990_clk_gate("pclk1_gate", "hclk_gate", base + REG_CLK_HCLKEN0, 13);
	hws[PCLK2] = nuc990_clk_fixed_factor("pclk2", "hclk_gate", 1, 2);
	hws[PCLK2_GATE] = nuc990_clk_gate("pclk2_gate", "pclk2", base + REG_CLK_HCLKEN0, 14);

	hws[HXT_DIV512] = nuc990_clk_fixed_factor("hxt_div512", "hxt_gate", 1, 512);
	hws[PCLK0_DIV2] = nuc990_clk_fixed_factor("pclk0_div2", "pclk0_gate", 1, 2);
	hws[PCLK0_DIV4K] = nuc990_clk_fixed_factor("pclk0_div4k", "pclk0_gate", 1, 4096);
	hws[PCLK1_DIV2] = nuc990_clk_fixed_factor("pclk1_div2", "pclk1_gate", 1, 2);
	hws[PCLK1_DIV4K] = nuc990_clk_fixed_factor("pclk1_div4k", "pclk1_gate", 1, 4096);
	hws[PCLK2_DIV4K] = nuc990_clk_fixed_factor("pclk2_div4k", "pclk2_gate", 1, 4096);

	hws[APLL_DIV] = nuc990_clk_fixed_factor("apll_div", "apll", 1, 2);
	hws[UPLL_DIV] = nuc990_clk_fixed_factor("upll_div", "upll", 1, 2);
	hws[PCLK3_MUX] = nuc990_clk_mux("pclk3_mux", base + REG_CLK_DIV0, 19, 2, pclk3_sel_clks, ARRAY_SIZE(pclk3_sel_clks));
	hws[PCLK3_DIV] = nuc990_clk_div_pow2("pclk3_div", "pclk3_mux", base + REG_CLK_DIV0, 16, 3);
	hws[PCLK3_GATE] = nuc990_clk_gate("pclk3_gate", "pclk3_div", base + REG_CLK_HCLKEN0, 15);

	hws[DDR_GATE] = nuc990_clk_gate_critical("ddr_gate", "sys_mux", base + REG_CLK_HCLKEN0, 10);
	hws[CLKO_MUX] = nuc990_clk_mux("clko_mux", base + REG_CLK_DIV9, 19, 2, clko_sel_clks, ARRAY_SIZE(clko_sel_clks));
	hws[CLKO_DIV] = nuc990_clk_divider("clko_div", "clko_mux", base + REG_CLK_DIV9, 24, 8);
	hws[CLKO_GATE] = nuc990_clk_gate("clko_gate", "clko_div", base + REG_CLK_HCLKEN0, 6);
	/* hclk hierarchy */
	hws[GPIO_GATE] = nuc990_clk_gate("gpio_gate", "hclk_gate", base + REG_CLK_HCLKEN0, 11);
	hws[EBI_GATE] = nuc990_clk_gate("ebi_gate", "hclk_gate", base + REG_CLK_HCLKEN0, 9);
	hws[SRAM_GATE] = nuc990_clk_gate_critical("sram_gate", "hclk_gate", base + REG_CLK_HCLKEN0, 8);
	hws[PDMA0_GATE] = nuc990_clk_gate("pdma0_gate", "hclk_gate", base + REG_CLK_HCLKEN0, 16);
	hws[PDMA1_GATE] = nuc990_clk_gate("pdma1_gate", "hclk_gate", base + REG_CLK_HCLKEN0, 17);
	hws[NAND_GATE] = nuc990_clk_gate("nand_gate", "hclk_gate", base + REG_CLK_HCLKEN0, 21);
	hws[SDH0_MUX] = nuc990_clk_mux("sdh0_mux", base + REG_CLK_DIV3, 3, 2, sdh0_sel_clks, ARRAY_SIZE(sdh0_sel_clks));
	hws[SDH0_DIV] = nuc990_clk_divider("sdh0_div", "sdh0_mux", base + REG_CLK_DIV3, 5, 11);
	hws[SDH0_GATE] = nuc990_clk_gate("sdh0_gate", "sdh0_div", base + REG_CLK_HCLKEN0, 22);
	hws[SDH1_MUX] = nuc990_clk_mux("sdh1_mux", base + REG_CLK_DIV3, 19, 2, sdh1_sel_clks, ARRAY_SIZE(sdh1_sel_clks));
	hws[SDH1_DIV] = nuc990_clk_divider("sdh1_div", "sdh1_mux", base + REG_CLK_DIV3, 21, 11);
	hws[SDH1_GATE] = nuc990_clk_gate("sdh1_gate", "sdh1_div", base + REG_CLK_HCLKEN0, 23);
	hws[USBH_GATE] = nuc990_clk_gate("usbh_gate", "hclk_gate", base + REG_CLK_HCLKEN0, 24);
	hws[USBD_GATE] = nuc990_clk_gate("usbd_gate", "hclk_gate", base + REG_CLK_HCLKEN0, 25);
	hws[EMAC0_GATE] = nuc990_clk_gate("emac0_gate", "hclk_gate", base + REG_CLK_HCLKEN0, 18);
	hws[EMAC1_GATE] = nuc990_clk_gate("emac1_gate", "hclk_gate", base + REG_CLK_HCLKEN0, 19);
	hws[I2S_MUX] = nuc990_clk_mux("i2s_mux", base + REG_CLK_DIV1, 19, 2, i2s_sel_clks, ARRAY_SIZE(i2s_sel_clks));
	hws[I2S_DIV] = nuc990_clk_divider("i2s_div", "i2s_mux", base + REG_CLK_DIV1, 24, 8);
	hws[I2S_GATE] = nuc990_clk_gate("i2s_gate", "i2s_div", base + REG_CLK_HCLKEN1, 0);
	hws[CRPT_GATE] = nuc990_clk_gate("crpt_gate", "hclk_gate", base + REG_CLK_HCLKEN1, 1);
	hws[OTP_GATE] = nuc990_clk_gate("otp_gate", "hclk_gate", base + REG_CLK_HCLKEN1, 3);
	hws[KS_GATE] = nuc990_clk_gate("ks_gate", "hclk_gate", base + REG_CLK_HCLKEN1, 2);
	hws[MDC_DIV] = nuc990_clk_divider("mdc_div", "hclk_gate", base + REG_CLK_DIV8, 0, 8);

	hws[CANFD0_MUX] = nuc990_clk_mux("canfd0_mux", base + REG_CLK_DIV2, 0, 1, canfd_sel_clks, ARRAY_SIZE(canfd_sel_clks));
	hws[CANFD0_DIV] = nuc990_clk_divider("canfd0_div", "canfd0_mux", base + REG_CLK_DIV7, 0, 4);
	hws[CANFD0_GATE] = nuc990_clk_gate("canfd0_gate", "canfd0_div", base + REG_CLK_HCLKEN1, 8);
	hws[CANFR0_GATE] = nuc990_clk_gate("canfr0_gate", "canfd0_div", base + REG_CLK_HCLKEN1, 16);
	hws[CANFD1_MUX] = nuc990_clk_mux("canfd1_mux", base + REG_CLK_DIV2, 1, 1, canfd_sel_clks, ARRAY_SIZE(canfd_sel_clks));
	hws[CANFD1_DIV] = nuc990_clk_divider("canfd1_div", "canfd1_mux", base + REG_CLK_DIV7, 4, 4);
	hws[CANFD1_GATE] = nuc990_clk_gate("canfd1_gate", "canfd1_div", base + REG_CLK_HCLKEN1, 9);
	hws[CANFR1_GATE] = nuc990_clk_gate("canfr1_gate", "canfd1_div", base + REG_CLK_HCLKEN1, 17);
	hws[CANFD2_MUX] = nuc990_clk_mux("canfd2_mux", base + REG_CLK_DIV2, 2, 1, canfd_sel_clks, ARRAY_SIZE(canfd_sel_clks));
	hws[CANFD2_DIV] = nuc990_clk_divider("canfd2_div", "canfd2_mux", base + REG_CLK_DIV7, 8, 4);
	hws[CANFD2_GATE] = nuc990_clk_gate("canfd2_gate", "canfd2_div", base + REG_CLK_HCLKEN1, 10);
	hws[CANFR2_GATE] = nuc990_clk_gate("canfr2_gate", "canfd2_div", base + REG_CLK_HCLKEN1, 18);
	hws[CANFD3_MUX] = nuc990_clk_mux("canfd3_mux", base + REG_CLK_DIV2, 3, 1, canfd_sel_clks, ARRAY_SIZE(canfd_sel_clks));
	hws[CANFD3_DIV] = nuc990_clk_divider("canfd3_div", "canfd3_mux", base + REG_CLK_DIV7, 12, 4);
	hws[CANFD3_GATE] = nuc990_clk_gate("canfd3_gate", "canfd3_div", base + REG_CLK_HCLKEN1, 11);
	hws[CANFR3_GATE] = nuc990_clk_gate("canfr3_gate", "canfd3_div", base + REG_CLK_HCLKEN1, 19);

	/* PCLK0 hierarchy */
	hws[ADC0_MUX] = nuc990_clk_mux("adc0_mux", base + REG_CLK_DIV7, 20, 2, adc0_sel_clks, ARRAY_SIZE(adc0_sel_clks));
	hws[ADC0_DIV] = nuc990_clk_divider("adc0_div", "adc0_mux", base + REG_CLK_DIV7, 24, 8);
	hws[ADC0_GATE] = nuc990_clk_gate("adc0_gate", "adc0_div", base + REG_CLK_PCLKEN1, 24);
	hws[EADC0_DIV] = nuc990_clk_divider("eadc0_div", "pclk0_gate", base + REG_CLK_DIV6, 16, 8);
	hws[EADC0_GATE] = nuc990_clk_gate("eadc0_gate", "eadc0_div", base + REG_CLK_PCLKEN1, 25);
	hws[TMR0_MUX] = nuc990_clk_mux("tmr0_mux", base + REG_CLK_DIV8, 16, 2, tmr0_sel_clks, ARRAY_SIZE(tmr0_sel_clks));
	hws[TMR0_GATE] = nuc990_clk_gate("tmr0_gate", "tmr0_mux", base + REG_CLK_PCLKEN0, 8);
	hws[TMR1_MUX] = nuc990_clk_mux("tmr1_mux", base + REG_CLK_DIV8, 18, 2, tmr1_sel_clks, ARRAY_SIZE(tmr1_sel_clks));
	hws[TMR1_GATE] = nuc990_clk_gate("tmr1_gate", "tmr1_mux", base + REG_CLK_PCLKEN0, 9);
	hws[TMR4_MUX] = nuc990_clk_mux("tmr4_mux", base + REG_CLK_DIV8, 24, 2, tmr4_sel_clks, ARRAY_SIZE(tmr4_sel_clks));
	hws[TMR4_GATE] = nuc990_clk_gate("tmr4_gate", "tmr4_mux", base + REG_CLK_PCLKEN0, 12);
	hws[TMR5_MUX] = nuc990_clk_mux("tmr5_mux", base + REG_CLK_DIV8, 26, 2, tmr5_sel_clks, ARRAY_SIZE(tmr5_sel_clks));
	hws[TMR5_GATE] = nuc990_clk_gate("tmr5_gate", "tmr5_mux", base + REG_CLK_PCLKEN0, 13);
	hws[BPWM0_GATE] = nuc990_clk_gate("bpwm0_gate", "pclk0_gate", base + REG_CLK_PCLKEN1, 26);
	hws[QSPI0_MUX] = nuc990_clk_mux("qspi0_mux", base + REG_CLK_DIV2, 8, 2, qspi0_sel_clks, ARRAY_SIZE(qspi0_sel_clks));
	hws[QSPI0_GATE] = nuc990_clk_gate("qspi0_gate", "qspi0_mux", base + REG_CLK_PCLKEN1, 20);
	hws[SPI0_MUX] = nuc990_clk_mux("spi0_mux", base + REG_CLK_DIV2, 10, 2, spi0_sel_clks, ARRAY_SIZE(spi0_sel_clks));
	hws[SPI0_GATE] = nuc990_clk_gate("spi0_gate", "spi0_mux", base + REG_CLK_PCLKEN1, 4);
	hws[SPI2_MUX] = nuc990_clk_mux("spi2_mux", base + REG_CLK_DIV2, 14, 2, spi2_sel_clks, ARRAY_SIZE(spi2_sel_clks));
	hws[SPI2_GATE] = nuc990_clk_gate("spi2_gate", "spi2_mux", base + REG_CLK_PCLKEN1, 6);
	hws[UART0_MUX] = nuc990_clk_mux("uart0_mux", base + REG_CLK_DIV4, 3, 2, uart_sel_clks, ARRAY_SIZE(uart_sel_clks));
	hws[UART0_DIV] = nuc990_clk_divider("uart0_div", "uart0_mux", base + REG_CLK_DIV4, 5, 3);
	hws[UART0_GATE] = nuc990_clk_gate("uart0_gate", "uart0_div", base + REG_CLK_PCLKEN0, 16);
	hws[UART2_MUX] = nuc990_clk_mux("uart2_mux", base + REG_CLK_DIV4, 19, 2, uart_sel_clks, ARRAY_SIZE(uart_sel_clks));
	hws[UART2_DIV] = nuc990_clk_divider("uart2_div", "uart2_mux", base + REG_CLK_DIV4, 21, 3);
	hws[UART2_GATE] = nuc990_clk_gate("uart2_gate", "uart2_div", base + REG_CLK_PCLKEN0, 18);
	hws[UART4_MUX] = nuc990_clk_mux("uart4_mux", base + REG_CLK_DIV5, 3, 2, uart_sel_clks, ARRAY_SIZE(uart_sel_clks));
	hws[UART4_DIV] = nuc990_clk_divider("uart4_div", "uart4_mux", base + REG_CLK_DIV5, 5, 3);
	hws[UART4_GATE] = nuc990_clk_gate("uart4_gate", "uart4_div", base + REG_CLK_PCLKEN0, 20);
	hws[UART6_MUX] = nuc990_clk_mux("uart6_mux", base + REG_CLK_DIV5, 19, 2, uart_sel_clks, ARRAY_SIZE(uart_sel_clks));
	hws[UART6_DIV] = nuc990_clk_divider("uart6_div", "uart6_mux", base + REG_CLK_DIV5, 21, 3);
	hws[UART6_GATE] = nuc990_clk_gate("uart6_gate", "uart6_div", base + REG_CLK_PCLKEN0, 22);
	hws[UART8_MUX] = nuc990_clk_mux("uart8_mux", base + REG_CLK_DIV6, 3, 2, uart_sel_clks, ARRAY_SIZE(uart_sel_clks));
	hws[UART8_DIV] = nuc990_clk_divider("uart8_div", "uart8_mux", base + REG_CLK_DIV6, 5, 3);
	hws[UART8_GATE] = nuc990_clk_gate("uart8_gate", "uart8_div", base + REG_CLK_PCLKEN0, 24);
	hws[I2C0_GATE] = nuc990_clk_gate("i2c0_gate", "pclk0_gate", base + REG_CLK_PCLKEN1, 0);
	hws[I2C2_GATE] = nuc990_clk_gate("i2c2_gate", "pclk0_gate", base + REG_CLK_PCLKEN1, 2);

	/* PCLK1 hierarchy */
	hws[TMR2_MUX] = nuc990_clk_mux("tmr2_mux", base + REG_CLK_DIV8, 20, 2, tmr2_sel_clks, ARRAY_SIZE(tmr2_sel_clks));
	hws[TMR2_GATE] = nuc990_clk_gate("tmr2_gate", "tmr2_mux", base + REG_CLK_PCLKEN0, 10);
	hws[TMR3_MUX] = nuc990_clk_mux("tmr3_mux", base + REG_CLK_DIV8, 22, 2, tmr3_sel_clks, ARRAY_SIZE(tmr3_sel_clks));
	hws[TMR3_GATE] = nuc990_clk_gate("tmr3_gate", "tmr3_mux", base + REG_CLK_PCLKEN0, 11);
	hws[BPWM1_GATE] = nuc990_clk_gate("bpwm1_gate", "pclk1_gate", base + REG_CLK_PCLKEN1, 27);
	hws[SPI1_MUX] = nuc990_clk_mux("spi1_mux", base + REG_CLK_DIV2, 12, 2, spi1_sel_clks, ARRAY_SIZE(spi1_sel_clks));
	hws[SPI1_GATE] = nuc990_clk_gate("spi1_gate", "spi1_mux", base + REG_CLK_PCLKEN1, 5);
	hws[SPI3_MUX] = nuc990_clk_mux("spi3_mux", base + REG_CLK_DIV2, 16, 2, spi3_sel_clks, ARRAY_SIZE(spi3_sel_clks));
	hws[SPI3_GATE] = nuc990_clk_gate("spi3_gate", "spi3_mux", base + REG_CLK_PCLKEN1, 7);
	hws[UART1_MUX] = nuc990_clk_mux("uart1_mux", base + REG_CLK_DIV4, 11, 2, uart_sel_clks, ARRAY_SIZE(uart_sel_clks));
	hws[UART1_DIV] = nuc990_clk_divider("uart1_div", "uart1_mux", base + REG_CLK_DIV4, 13, 3);
	hws[UART1_GATE] = nuc990_clk_gate("uart1_gate", "uart1_div", base + REG_CLK_PCLKEN0, 17);
	hws[UART3_MUX] = nuc990_clk_mux("uart3_mux", base + REG_CLK_DIV4, 27, 2, uart_sel_clks, ARRAY_SIZE(uart_sel_clks));
	hws[UART3_DIV] = nuc990_clk_divider("uart3_div", "uart3_mux", base + REG_CLK_DIV4, 29, 3);
	hws[UART3_GATE] = nuc990_clk_gate("uart3_gate", "uart3_div", base + REG_CLK_PCLKEN0, 19);
	hws[UART5_MUX] = nuc990_clk_mux("uart5_mux", base + REG_CLK_DIV5, 11, 2, uart_sel_clks, ARRAY_SIZE(uart_sel_clks));
	hws[UART5_DIV] = nuc990_clk_divider("uart5_div", "uart5_mux", base + REG_CLK_DIV5, 13, 3);
	hws[UART5_GATE] = nuc990_clk_gate("uart5_gate", "uart5_div", base + REG_CLK_PCLKEN0, 21);
	hws[UART7_MUX] = nuc990_clk_mux("uart7_mux", base + REG_CLK_DIV5, 27, 2, uart_sel_clks, ARRAY_SIZE(uart_sel_clks));
	hws[UART7_DIV] = nuc990_clk_divider("uart7_div", "uart7_mux", base + REG_CLK_DIV5, 29, 3);
	hws[UART7_GATE] = nuc990_clk_gate("uart7_gate", "uart7_div", base + REG_CLK_PCLKEN0, 23);
	hws[UART9_MUX] = nuc990_clk_mux("uart9_mux", base + REG_CLK_DIV6, 11, 2, uart_sel_clks, ARRAY_SIZE(uart_sel_clks));
	hws[UART9_DIV] = nuc990_clk_divider("uart9_div", "uart9_mux", base + REG_CLK_DIV6, 13, 3);
	hws[UART9_GATE] = nuc990_clk_gate("uart9_gate", "uart9_div", base + REG_CLK_PCLKEN0, 25);
	hws[I2C1_GATE] = nuc990_clk_gate("i2c1_gate", "pclk1_gate", base + REG_CLK_PCLKEN1, 1);
	hws[I2C3_GATE] = nuc990_clk_gate("i2c3_gate", "pclk1_gate", base + REG_CLK_PCLKEN1, 3);
	/* PCLK2 hierarchy */
	hws[WDT0_MUX] = nuc990_clk_mux("wdt0_mux", base + REG_CLK_DIV8, 8, 2, wdt0_sel_clks, ARRAY_SIZE(wdt0_sel_clks));
	hws[WDT0_GATE] = nuc990_clk_gate("wdt0_gate", "wdt0_mux", base + REG_CLK_PCLKEN0, 0);
	hws[WWDT0_MUX] = nuc990_clk_mux("wwdt0_mux", base + REG_CLK_DIV8, 10, 2, wwdt0_sel_clks, ARRAY_SIZE(wwdt0_sel_clks));
	hws[WWDT0_GATE] = nuc990_clk_gate("wwdt0_gate", "wwdt0_mux", base + REG_CLK_PCLKEN0, 1);
	hws[SMC0_DIV] = nuc990_clk_divider("smc0_div", "pclk2_gate", base + REG_CLK_DIV6, 24, 4);
	hws[SMC0_GATE] = nuc990_clk_gate("smc0_gate", "smc0_div", base + REG_CLK_PCLKEN1, 16);
	hws[SMC1_DIV] = nuc990_clk_divider("smc1_div", "pclk2_gate", base + REG_CLK_DIV6, 28, 4);
	hws[SMC1_GATE] = nuc990_clk_gate("smc1_gate", "smc1_div", base + REG_CLK_PCLKEN1, 17);
	hws[RTC_GATE] = nuc990_clk_gate("rtc_gate", "lxt", base + REG_CLK_PCLKEN0, 2);
	hws[AIC_GATE] = nuc990_clk_gate_critical("aic_gate", "pclk2_gate", base + REG_CLK_PCLKEN0, 3);
	/* PCLK3 hierarchy */
	hws[CAN0_GATE] = nuc990_clk_gate("can0_gate", "pclk3_gate", base + REG_CLK_PCLKEN1, 8);
	hws[CAN1_GATE] = nuc990_clk_gate("can1_gate", "pclk3_gate", base + REG_CLK_PCLKEN1, 9);
	hws[CAN2_GATE] = nuc990_clk_gate("can2_gate", "pclk3_gate", base + REG_CLK_PCLKEN1, 10);
	hws[CAN3_GATE] = nuc990_clk_gate("can3_gate", "pclk3_gate", base + REG_CLK_PCLKEN1, 11);

	ret = of_clk_add_hw_provider(np, of_clk_hw_onecell_get, nuc990_hw_data);
	if (ret < 0) {
		pr_err("failed to register hws for NUC990\n");
		iounmap(base);
	}

	/* register power off callback */
	clk_base = base;
	pm_power_off = nuc990_pm_power_off;
}
CLK_OF_DECLARE(nuc990_clk, "nuvoton,nuc990-clk", nuc990_clk_init);

MODULE_AUTHOR("Joey Lu <yclu4@nuvoton.com>");
MODULE_DESCRIPTION("NUVOTON NUC990 Clock Driver");
MODULE_LICENSE("GPL v2");
