/* SPDX-License-Identifier: GPL-2.0 */
/*
 * Copyright (c) 2025 Nuvoton technology corporation.
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation;version 2 of the License.
 *
 */

#ifndef _NUC990_I2S_H
#define _NUC990_I2S_H

#include <linux/gpio/consumer.h>
#include <sound/dmaengine_pcm.h>
#include <linux/io.h>

#define IN	0
#define OUT	1
/* NUC990 I2S Controller Register Map */
#define NUC990_I2S_BA               0xB0020000

#define NUC990_I2S_GLBCON           0x000
#define NUC990_I2S_RESET            0x004
#define NUC990_I2S_RDESB            0x008
#define NUC990_I2S_RDES_LENGTH      0x00C
#define NUC990_I2S_RDESC            0x010
#define NUC990_I2S_PDESB            0x014
#define NUC990_I2S_PDES_LENGTH      0x018
#define NUC990_I2S_PDESC            0x01C
#define NUC990_I2S_RSR              0x020
#define NUC990_I2S_PSR              0x024
#define NUC990_I2S_CON              0x028
#define NUC990_I2S_COUNTER          0x02C
#define NUC990_I2S_PCMCON           0x030
#define NUC990_I2S_PCMS1ST          0x034
#define NUC990_I2S_PCMS2ST          0x038
#define NUC990_I2S_RDESB2           0x040
#define NUC990_I2S_PDESB2           0x044

/* GLBCON               */
#define GLBCON_BLOCK_EN_SHIFT          0
#define GLBCON_BLOCK_EN_MASK           GENMASK(1, 0)
#define GLBCON_BLOCK_EN_DISABLE        (0x0 << GLBCON_BLOCK_EN_SHIFT)
#define GLBCON_BLOCK_EN_I2S            (0x1 << GLBCON_BLOCK_EN_SHIFT)
#define GLBCON_BLOCK_EN_PCM            (0x2 << GLBCON_BLOCK_EN_SHIFT)
#define GLBCON_IRQ_DMA_DATA_ZERO_EN    BIT(3)
#define GLBCON_IRQ_DMA_CNTER_EN        BIT(4)
#define GLBCON_FIFO_TH                 BIT(7)   /* 0: 8-level, 1: 4-level */
#define GLBCON_BITS_SELECT_SHIFT       8
#define GLBCON_BITS_SELECT_MASK        GENMASK(9, 8)
#define GLBCON_BITS_SELECT_8BIT        (0x0 << GLBCON_BITS_SELECT_SHIFT)
#define GLBCON_BITS_SELECT_16BIT       (0x1 << GLBCON_BITS_SELECT_SHIFT)
#define GLBCON_BITS_SELECT_24BIT       (0x2 << GLBCON_BITS_SELECT_SHIFT)
#define GLBCON_P_DMA_IRQ               BIT(10)
#define GLBCON_R_DMA_IRQ               BIT(11)
#define GLBCON_P_DMA_IRQ_SEL_SHIFT     12
#define GLBCON_P_DMA_IRQ_SEL_MASK      GENMASK(13, 12)
#define GLBCON_P_DMA_IRQ_SEL_END       (0x0 << GLBCON_P_DMA_IRQ_SEL_SHIFT)
#define GLBCON_P_DMA_IRQ_SEL_HALF      (0x1 << GLBCON_P_DMA_IRQ_SEL_SHIFT)
#define GLBCON_P_DMA_IRQ_SEL_QUARTER   (0x2 << GLBCON_P_DMA_IRQ_SEL_SHIFT)
#define GLBCON_P_DMA_IRQ_SEL_EIGHTH    (0x3 << GLBCON_P_DMA_IRQ_SEL_SHIFT)
#define GLBCON_R_DMA_IRQ_SEL_SHIFT     14
#define GLBCON_R_DMA_IRQ_SEL_MASK      GENMASK(15, 14)
#define GLBCON_R_DMA_IRQ_SEL_END       (0x0 << GLBCON_R_DMA_IRQ_SEL_SHIFT)
#define GLBCON_R_DMA_IRQ_SEL_HALF      (0x1 << GLBCON_R_DMA_IRQ_SEL_SHIFT)
#define GLBCON_R_DMA_IRQ_SEL_QUARTER   (0x2 << GLBCON_R_DMA_IRQ_SEL_SHIFT)
#define GLBCON_R_DMA_IRQ_SEL_EIGHTH    (0x3 << GLBCON_R_DMA_IRQ_SEL_SHIFT)
#define GLBCON_P_FIFO_EMPTY_IRQ_EN     BIT(16)
#define GLBCON_P_FIFO_FULL_IRQ_EN      BIT(17)
#define GLBCON_R_FIFO_EMPTY_IRQ_EN     BIT(18)
#define GLBCON_R_FIFO_FULL_IRQ_EN      BIT(19)
#define GLBCON_P_DMA_IRQ_EN            BIT(20)
#define GLBCON_R_DMA_IRQ_EN            BIT(21)

/* RESET              */
#define RESET_BLOCK_RESET_SHIFT        0
#define RESET_BLOCK_RESET_MASK         BIT(0)
#define RESET_BLOCK_RESET_RELEASE      (0x0 << RESET_BLOCK_RESET_SHIFT) /* Release I2S/PCM block from reset */
#define RESET_BLOCK_RESET_ASSERT       (0x1 << RESET_BLOCK_RESET_SHIFT) /* Force I2S/PCM block to reset */
#define RESET_DMA_DATA_ZERO_EN         BIT(3)   /* 0: Disabled, 1: Enabled */
#define RESET_DMA_CNTER_EN             BIT(4)   /* 0: Disabled, 1: Enabled */
#define RESET_PLAY                     BIT(5)   /* 0: Disabled, 1: Enabled */
#define RESET_RECORD                   BIT(6)   /* 0: Disabled, 1: Enabled */
#define RESET_PLAY_SINGLE_SHIFT        12
#define RESET_PLAY_SINGLE_MASK         GENMASK(13, 12)
#define RESET_PLAY_SINGLE_RESERVED0    (0x0 << RESET_PLAY_SINGLE_SHIFT)
#define RESET_PLAY_SINGLE_RESERVED1    (0x1 << RESET_PLAY_SINGLE_SHIFT)
#define RESET_PLAY_SINGLE_MONO         (0x2 << RESET_PLAY_SINGLE_SHIFT)
#define RESET_PLAY_SINGLE_STEREO       (0x3 << RESET_PLAY_SINGLE_SHIFT)
#define RESET_RECORD_SINGLE_SHIFT      14
#define RESET_RECORD_SINGLE_MASK       GENMASK(15, 14)
#define RESET_RECORD_SINGLE_RESERVED   (0x0 << RESET_RECORD_SINGLE_SHIFT)
#define RESET_RECORD_SINGLE_LEFT       (0x1 << RESET_RECORD_SINGLE_SHIFT)
#define RESET_RECORD_SINGLE_RIGHT      (0x2 << RESET_RECORD_SINGLE_SHIFT)
#define RESET_RECORD_SINGLE_DUAL       (0x3 << RESET_RECORD_SINGLE_SHIFT)
#define RESET_RESET                    BIT(16)  /* 0: Normal operation, 1: Whole audio controller reset */
#define RESET_SPLIT_DATA               BIT(20)  /* 0: Normal L/R, 1: Split L/R and Slot1/Slot2 data */

/* RSR              */
#define RSR_R_DMA_RIA_IRQ              BIT(0)  /* Record DMA reach indicative address IRQ */
#define RSR_R_FIFO_EMPTY               BIT(1)  /* Record FIFO empty indicator */
#define RSR_R_FIFO_FULL                BIT(2)  /* Record FIFO full indicator */
#define RSR_R_DMA_RIA_SN_SHIFT         5
#define RSR_R_DMA_RIA_SN_MASK          GENMASK(7, 5) /* Record DMA reach indicative address section number */

/* PSR              */
#define PSR_P_DMA_RIA_IRQ              BIT(0)  /* Playback DMA reach indicative address IRQ */
#define PSR_P_FIFO_EMPTY               BIT(1)  /* Playback FIFO empty indicator */
#define PSR_P_FIFO_FULL                BIT(2)  /* Playback FIFO full indicator */
#define PSR_DMA_DATA_ZERO_IRQ          BIT(3)  /* DMA data zero IRQ */
#define PSR_DMA_CNTER_IRQ              BIT(4)  /* DMA counter IRQ */
#define PSR_P_DMA_RIA_SN_SHIFT         5
#define PSR_P_DMA_RIA_SN_MASK          GENMASK(7, 5) /* Playback DMA reach indicative address section number */

/* CON				*/
#define CON_FORMAT                     BIT(3)  /* 0: I2S compatible format, 1: MSB-justified format */
#define CON_MCLK_SEL                    BIT(4)  /* 0: MCLK follows PRS, 1: MCLK same as PLL input */
#define CON_BCLK_DIV_SHIFT              5
#define CON_BCLK_DIV_MASK               GENMASK(7, 5) /* I2S serial data clock frequency selection */
#define CON_PRS_SHIFT                  16
#define CON_PRS_MASK                   GENMASK(19, 16)
#define CON_PRS_SET(x)                 ((x) << CON_PRS_SHIFT)
#define CON_SLAVE                       BIT(20) /* 0: Master mode, 1: Slave mode */

/* PCMCON			*/
#define PCMCON_BCLKP                   BIT(0)  /* 0: Data on rising edge, latch on falling edge; 1: Data on falling edge, latch on rising edge */
#define PCMCON_PCM_PRS_SHIFT           8
#define PCMCON_PCM_PRS_MASK            GENMASK(15, 8) /* PCM_BCLK frequency pre-scaler selection */
#define PCMCON_FS_PERIOD_SHIFT         16
#define PCMCON_FS_PERIOD_MASK          GENMASK(25, 16) /* FS pulse period, used for sample rate */
#define PCMCON_PCM_MCLK_PRS_SHIFT      28
#define PCMCON_PCM_MCLK_PRS_MASK       GENMASK(31, 28)
#define PCMCON_PCM_MCLK_PRS_SET(x)     ((x) << PCMCON_PCM_MCLK_PRS_SHIFT)

/* PCMS1ST			*/
#define PCMS1ST_SLOT1_I_START_SHIFT    0
#define PCMS1ST_SLOT1_I_START_MASK     GENMASK(9, 0)   /* Slot 1 data input start position */
#define PCMS1ST_SLOT1_O_START_SHIFT    16
#define PCMS1ST_SLOT1_O_START_MASK     GENMASK(25, 16) /* Slot 1 data output start position */

/* PCMS2ST			*/
#define PCMS2ST_SLOT2_I_START_SHIFT    0
#define PCMS2ST_SLOT2_I_START_MASK     GENMASK(9, 0)   /* Slot 2 data input start position */
#define PCMS2ST_SLOT2_O_START_SHIFT    16
#define PCMS2ST_SLOT2_O_START_MASK     GENMASK(25, 16) /* Slot 2 data output start position */

#define NUC990_AUDIO_SAMPLECLK  0x00
#define NUC990_AUDIO_CLKDIV     0x01

struct nuc990_i2s_info {
	void __iomem *mmio;
	spinlock_t irqlock, lock;
	dma_addr_t dma_addr[2];
	unsigned long buffersize[2];
	unsigned long irq_num;
	struct snd_pcm_substream *substream[2];
	struct resource *res;
	struct clk *clk;
	struct device *dev;
	unsigned int mclk;
};

#define DRV_NAME "nuc990-dai"

static inline void nuc990_i2s_write_reg(struct nuc990_i2s_info *info,
					unsigned int reg, unsigned int val)
{
	writel_relaxed(val, info->mmio + reg);
}

static inline unsigned int nuc990_i2s_read_reg(struct nuc990_i2s_info *info,
					   unsigned int reg)
{
	return readl_relaxed(info->mmio + reg);
}

#endif /*end _NUC990_I2S_H */
