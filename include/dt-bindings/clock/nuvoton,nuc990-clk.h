/* SPDX-License-Identifier: GPL-2.0+ OR MIT */
/*
 * Copyright (c) 2025 Nuvoton Technology Corporation.
 */

#ifndef __DT_BINDINGS_NUC990_CLK_H
#define __DT_BINDINGS_NUC990_CLK_H

/* Clock Sources */
#define HXT			0 // fixed
#define HXT_GATE	1
#define LXT			2 // fixed

/* PLLs */
#define	APLL		3
#define	UPLL		4
#define APLL_DIV	5
#define UPLL_DIV	6

/* CPU Clock, System Clock, HCLK and PCLK */
#define SYS_MUX		7 // DIVCTL0_SYSTEM_S
#define CPU_DIV		8 // DIVCTL0_CPUDIV2EN
#define CPU_GATE	9 // HCLKEN0_CPUCKEN
#define HCLK		10
#define HCLK_GATE	11
#define HCLK1		12
#define HCLK1_GATE	13
#define PCLK0_GATE	14
#define PCLK1_GATE	15
#define PCLK2		16
#define PCLK2_GATE	17
#define PCLK3_MUX	18
#define PCLK3_DIV	19
#define PCLK3_GATE	20

#define HXT_DIV512	21
#define PCLK0_DIV2	22
#define PCLK0_DIV4K	23
#define PCLK1_DIV2	24
#define PCLK1_DIV4K	25
#define PCLK2_DIV4K	26

/* Peripheral clocks */
#define DDR_GATE	27
#define CLKO_MUX	28
#define CLKO_DIV	29
#define CLKO_GATE	30
/* HCLK */
#define GPIO_GATE	31
#define EBI_GATE	32
#define SRAM_GATE	33
#define PDMA0_GATE	34
#define PDMA1_GATE	35
#define NAND_GATE	36
#define SDH0_MUX	37
#define SDH0_DIV	38
#define SDH0_GATE	39
#define SDH1_MUX	40
#define SDH1_DIV	41
#define SDH1_GATE	42
#define USBH_GATE	43
#define USBD_GATE	44
#define EMAC0_GATE	45
#define EMAC1_GATE	46
#define I2S_MUX		47
#define I2S_DIV		48
#define I2S_GATE	49
#define CRPT_GATE	50
#define OTP_GATE	51
#define KS_GATE		52
#define MDC_DIV		53
/* HCLK1 */
#define CANFD0_MUX	54
#define CANFD0_DIV	55
#define CANFD0_GATE	56
#define CANFR0_GATE	57
#define CANFD1_MUX	58
#define CANFD1_DIV	59
#define CANFD1_GATE	60
#define CANFR1_GATE	61
#define CANFD2_MUX	62
#define CANFD2_DIV	63
#define CANFD2_GATE	64
#define CANFR2_GATE	65
#define CANFD3_MUX	66
#define CANFD3_DIV	67
#define CANFD3_GATE	68
#define CANFR3_GATE	69
/* PCLK0 */
#define ADC0_MUX	70
#define ADC0_DIV	71
#define ADC0_GATE	72
#define EADC0_DIV	73
#define EADC0_GATE	74
#define TMR0_MUX	75
#define TMR0_GATE	76
#define TMR1_MUX	77
#define TMR1_GATE	78
#define TMR4_MUX	79
#define TMR4_GATE	80
#define TMR5_MUX	81
#define TMR5_GATE	82
#define BPWM0_GATE	83
#define QSPI0_MUX	84
#define QSPI0_GATE	85
#define SPI0_MUX	86
#define SPI0_GATE	87
#define SPI2_MUX	88
#define SPI2_GATE	89
#define UART0_MUX	90
#define UART0_DIV	91
#define UART0_GATE	92
#define UART2_MUX	93
#define UART2_DIV	94
#define UART2_GATE	95
#define UART4_MUX	96
#define UART4_DIV	97
#define UART4_GATE	98
#define UART6_MUX	99
#define UART6_DIV	100
#define UART6_GATE	101
#define UART8_MUX	102
#define UART8_DIV	103
#define UART8_GATE	104
#define I2C0_GATE	105
#define I2C2_GATE	106
/* PCLK1 */
#define TMR2_MUX	107
#define TMR2_GATE	108
#define TMR3_MUX	109
#define TMR3_GATE	110
#define BPWM1_GATE	111
#define SPI1_MUX	112
#define SPI1_GATE	113
#define SPI3_MUX	114
#define SPI3_GATE	115
#define UART1_MUX	116
#define UART1_DIV	117
#define UART1_GATE	118
#define UART3_MUX	119
#define UART3_DIV	120
#define UART3_GATE	121
#define UART5_MUX	122
#define UART5_DIV	123
#define UART5_GATE	124
#define UART7_MUX	125
#define UART7_DIV	126
#define UART7_GATE	127
#define UART9_MUX	128
#define UART9_DIV	129
#define UART9_GATE	130
#define I2C1_GATE	131
#define I2C3_GATE	132
/* PCLK2 */
#define WDT0_MUX	133
#define WDT0_GATE	134
#define WWDT0_MUX	135
#define WWDT0_GATE	136
#define SMC0_DIV	137
#define SMC0_GATE	138
#define SMC1_DIV	139
#define SMC1_GATE	140
#define RTC_GATE	141
#define AIC_GATE	142
/* PCLK3 */
#define CAN0_GATE	143
#define CAN1_GATE	144
#define CAN2_GATE	145
#define CAN3_GATE	146

/* place holder */
#define NUC990_CLK_IDX	147

#endif /* __DT_BINDINGS_NUC990_CLK_H */
