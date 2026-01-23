// SPDX-License-Identifier: GPL-2.0
/*
 * Copyright (c) 2025 Nuvoton technology corporation.
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation;version 2 of the License.
 *
 */

#include <linux/module.h>
#include <linux/dma-mapping.h>
#include <linux/delay.h>
#include <sound/core.h>
#include <sound/dmaengine_pcm.h>
#include <sound/pcm.h>
#include <sound/soc.h>
#include <sound/pcm_params.h>
#include <sound/soc-dai.h>
#include <linux/device.h>
#include <linux/clk.h>
#include <linux/of.h>
#include <linux/pm_runtime.h>

#include "nuc990-i2s.h"

static int nuc990_i2s_hw_params(struct snd_pcm_substream *substream,
				struct snd_pcm_hw_params *params,
				struct snd_soc_dai *dai)
{
	struct nuc990_i2s_info *info = snd_soc_dai_get_drvdata(dai);
	unsigned long val = nuc990_i2s_read_reg(info, NUC990_I2S_GLBCON);

	switch (params_width(params)) {
	case 8:
		val = (val & ~GLBCON_BITS_SELECT_MASK) |
		      GLBCON_BITS_SELECT_8BIT;
		break;

	case 16:
		val = (val & ~GLBCON_BITS_SELECT_MASK) |
		      GLBCON_BITS_SELECT_16BIT;
		break;

	case 24:
		val = (val & ~GLBCON_BITS_SELECT_MASK) |
		      GLBCON_BITS_SELECT_24BIT;
		break;

	default:
		return -EINVAL;
	}
	nuc990_i2s_write_reg(info, NUC990_I2S_GLBCON, val);
	return 0;
}

static int nuc990_i2s_set_fmt(struct snd_soc_dai *cpu_dai, unsigned int fmt)
{
	struct nuc990_i2s_info *info = snd_soc_dai_get_drvdata(cpu_dai);
	unsigned long val = nuc990_i2s_read_reg(info, NUC990_I2S_CON);

	switch (fmt & SND_SOC_DAIFMT_FORMAT_MASK) {
	case SND_SOC_DAIFMT_MSB:
		val |= CON_FORMAT;
		break;
	case SND_SOC_DAIFMT_I2S:
		val &= ~CON_FORMAT;
		break;
	default:
		return -EINVAL;
	}

	switch (fmt & SND_SOC_DAIFMT_MASTER_MASK) {
	case SND_SOC_DAIFMT_CBM_CFM:
		val &= ~CON_SLAVE;
		break;
	case SND_SOC_DAIFMT_CBS_CFS:
		val |= CON_SLAVE;
		break;
	default:
		return -EINVAL;
	}

	nuc990_i2s_write_reg(info, NUC990_I2S_CON, val);

	return 0;
}

static int nuc990_i2s_set_sysclk(struct snd_soc_dai *cpu_dai, int clk_id,
				 unsigned int freq, int dir)
{
	struct nuc990_i2s_info *info = snd_soc_dai_get_drvdata(cpu_dai);
	unsigned int val;
	struct clk *clkapll;
	unsigned int mclkdiv, bclkdiv, mclk;

	val = nuc990_i2s_read_reg(info, NUC990_I2S_CON);

	mclk = (freq * 256); // 256fs
	mclkdiv = clk_get_rate(info->clk) / mclk;
	val = (val & ~CON_PRS_MASK) | CON_PRS_SET(mclkdiv - 1);

	bclkdiv = mclk / (freq * cpu_dai->sample_bits * 2);
	bclkdiv = bclkdiv / 2 - 1;
	val = (val & ~CON_BCLK_DIV_MASK) | (bclkdiv << CON_BCLK_DIV_SHIFT);

	nuc990_i2s_write_reg(info, NUC990_I2S_CON, val);

	//use APLL to generate 12.288MHz ,16.934MHz or 11.285Mhz for I2S
	//input source clock is XIN=12Mhz

	clkapll = clk_get_parent(info->clk);

	if ((freq % 8000 == 0) && (freq != 32000)) {
		//12.288MHz ==> APLL=98.4MHz / 8 = 12.3MHz
		clk_set_rate(clkapll, 98400000);
		clk_set_rate(info->clk, 12300000);
	} else if (freq == 44100) {
		//16.934MHz ==> APLL=169.5MHz / 15 = 11.30MHz
		clk_set_rate(clkapll, 169500000);
		clk_set_rate(info->clk, 11300000);
	} else {
		//16.934MHz ==> APLL=169.5MHz / 10 = 16.95MHz
		clk_set_rate(clkapll, 169500000);
		clk_set_rate(info->clk, 16950000);
	}
	return 0;
}

static int nuc990_i2s_trigger(struct snd_pcm_substream *substream, int cmd,
			      struct snd_soc_dai *dai)
{
	struct nuc990_i2s_info *info = snd_soc_dai_get_drvdata(dai);
	int ret = 0;
	unsigned long val, con;

	con = nuc990_i2s_read_reg(info, NUC990_I2S_GLBCON);

	switch (cmd) {
	case SNDRV_PCM_TRIGGER_START:
	case SNDRV_PCM_TRIGGER_RESUME:
		val = nuc990_i2s_read_reg(info, NUC990_I2S_RESET);
		con |= GLBCON_BLOCK_EN_I2S;
		if (substream->stream == SNDRV_PCM_STREAM_PLAYBACK) {
			con |= GLBCON_P_DMA_IRQ_EN;
			nuc990_i2s_write_reg(info, NUC990_I2S_PSR,
					     PSR_P_DMA_RIA_IRQ);

			val |= RESET_PLAY;
		} else {
			con |= GLBCON_R_DMA_IRQ_EN;
			nuc990_i2s_write_reg(info, NUC990_I2S_RSR,
					     RSR_R_DMA_RIA_IRQ);

			val |= RESET_RECORD;
		}
		nuc990_i2s_write_reg(info, NUC990_I2S_RESET, val);
		nuc990_i2s_write_reg(info, NUC990_I2S_GLBCON, con);

		break;
	case SNDRV_PCM_TRIGGER_STOP:
	case SNDRV_PCM_TRIGGER_SUSPEND:
		val = nuc990_i2s_read_reg(info, NUC990_I2S_RESET);
		con &= ~GLBCON_BLOCK_EN_MASK;
		if (substream->stream == SNDRV_PCM_STREAM_PLAYBACK) {
			con &= ~GLBCON_P_DMA_IRQ_EN;
			nuc990_i2s_write_reg(info, NUC990_I2S_PSR, 0);
			val &= ~RESET_PLAY;
		} else {
			con &= ~GLBCON_R_DMA_IRQ_EN;
			nuc990_i2s_write_reg(info, NUC990_I2S_RSR, 0);
			val &= ~RESET_RECORD;
		}

		nuc990_i2s_write_reg(info, NUC990_I2S_RESET, val);
		nuc990_i2s_write_reg(info, NUC990_I2S_GLBCON, con);

		break;
	default:
		ret = -EINVAL;
	}

	return ret;
}

static int nuc990_i2s_probe(struct snd_soc_dai *dai)
{
	struct nuc990_i2s_info *info = snd_soc_dai_get_drvdata(dai);
	unsigned long val;

	/* Select I2S pins */
	val = nuc990_i2s_read_reg(info, NUC990_I2S_GLBCON);
	val = (val & ~GLBCON_BITS_SELECT_MASK) |
	      GLBCON_BITS_SELECT_16BIT; //set default data bit to 16-bit
	nuc990_i2s_write_reg(info, NUC990_I2S_GLBCON, val);

	return 0;
}

static void nuc990_i2s_enable(struct nuc990_i2s_info *info)
{
	unsigned long val = nuc990_i2s_read_reg(info, NUC990_I2S_GLBCON);

	if ((val & GLBCON_BLOCK_EN_MASK) == 0) {
		/* Enable clocks */
		clk_prepare_enable(info->clk);

		nuc990_i2s_write_reg(info, NUC990_I2S_RESET,
				     RESET_BLOCK_RESET_ASSERT);
		udelay(10);
		nuc990_i2s_write_reg(info, NUC990_I2S_RESET,
				     RESET_BLOCK_RESET_RELEASE);
		/* Enable i2s */
		val = (val & ~GLBCON_BLOCK_EN_MASK) | GLBCON_BLOCK_EN_I2S;

		nuc990_i2s_write_reg(info, NUC990_I2S_GLBCON, val);
	}
}

static void nuc990_i2s_disable(struct nuc990_i2s_info *info)
{
	unsigned long val = nuc990_i2s_read_reg(info, NUC990_I2S_GLBCON);

	/* Disable i2s */
	val &= ~GLBCON_BLOCK_EN_MASK;

	nuc990_i2s_write_reg(info, NUC990_I2S_GLBCON, val);

	/* Disable clocks */
	clk_disable_unprepare(info->clk);
}

static const struct snd_soc_dai_ops nuc990_i2s_dai_ops = {
	.probe = nuc990_i2s_probe,
	.trigger = nuc990_i2s_trigger,
	.hw_params = nuc990_i2s_hw_params,
	.set_fmt = nuc990_i2s_set_fmt,
	.set_sysclk = nuc990_i2s_set_sysclk,
};

struct snd_soc_dai_driver nuc990_i2s_dai = {
	.name		= "nuc990-i2s-dai",
	.symmetric_rate = 1,
	.playback = {
		.rates      = SNDRV_PCM_RATE_8000_48000,
		.formats    = SNDRV_PCM_FMTBIT_S16_LE,
		.channels_min   = 1,
		.channels_max   = 2,
	},
	.capture = {
		.rates      = SNDRV_PCM_RATE_8000_48000,
		.formats    = SNDRV_PCM_FMTBIT_S16_LE,
		.channels_min   = 1,
		.channels_max   = 2,
	},
	.ops = &nuc990_i2s_dai_ops,
};

static const struct snd_pcm_hardware nuc990_pcm_hardware = {
	.info = SNDRV_PCM_INFO_INTERLEAVED | SNDRV_PCM_INFO_BLOCK_TRANSFER |
		SNDRV_PCM_INFO_MMAP | SNDRV_PCM_INFO_MMAP_VALID |
		SNDRV_PCM_INFO_PAUSE | SNDRV_PCM_INFO_RESUME,
	.formats = SNDRV_PCM_FMTBIT_S16_LE | SNDRV_PCM_FMTBIT_S24_LE,
	.rates = SNDRV_PCM_RATE_32000 | SNDRV_PCM_RATE_44100 |
		 SNDRV_PCM_RATE_48000,
	.channels_min = 1,
	.channels_max = 2,
	.buffer_bytes_max = 4 * 1024,
	.period_bytes_min = 1 * 1024,
	.period_bytes_max = 4 * 1024,
	.periods_min = 1,
	.periods_max = 1024,
};

static int nuc990_dma_hw_params(struct snd_soc_component *component,
				struct snd_pcm_substream *substream,
				struct snd_pcm_hw_params *params)
{
	struct snd_pcm_runtime *runtime = substream->runtime;
	struct nuc990_i2s_info *info = runtime->private_data;
	unsigned long flags;
	int ret = 0;

	spin_lock_irqsave(&info->irqlock, flags);

	if (runtime->dma_addr == 0) {
		ret = snd_pcm_lib_malloc_pages(substream,
					       params_buffer_bytes(params));
		if (ret < 0)
			return ret;
		info->substream[substream->stream] = substream;
	}

	info->dma_addr[substream->stream] = runtime->dma_addr | 0x80000000;
	info->buffersize[substream->stream] = params_buffer_bytes(params);

	spin_unlock_irqrestore(&info->irqlock, flags);

	return ret;
}

static void nuc990_update_dma_register(struct snd_pcm_substream *substream,
				       dma_addr_t dma_addr, size_t count)
{
	struct snd_pcm_runtime *runtime = substream->runtime;
	struct nuc990_i2s_info *info = runtime->private_data;

	if (substream->stream == SNDRV_PCM_STREAM_PLAYBACK) {
		nuc990_i2s_write_reg(info, NUC990_I2S_PDESB, dma_addr);
		nuc990_i2s_write_reg(info, NUC990_I2S_PDES_LENGTH, count);
	} else {
		nuc990_i2s_write_reg(info, NUC990_I2S_RDESB, dma_addr);
		nuc990_i2s_write_reg(info, NUC990_I2S_RDES_LENGTH, count);
	}
}

static irqreturn_t nuc990_dma_interrupt(int irq, void *dev_id)
{
	struct nuc990_i2s_info *info = dev_id;
	unsigned long val;
	unsigned long flags;
	int stream;

	spin_lock_irqsave(&info->irqlock, flags);

	val = nuc990_i2s_read_reg(info, NUC990_I2S_CON);

	if (val & GLBCON_R_DMA_IRQ) {
		stream = SNDRV_PCM_STREAM_CAPTURE;
		nuc990_i2s_write_reg(info, NUC990_I2S_CON,
				     val | GLBCON_R_DMA_IRQ);

		val = nuc990_i2s_read_reg(info, NUC990_I2S_RSR);

		if (val & RSR_R_DMA_RIA_IRQ) {
			val = RSR_R_DMA_RIA_IRQ;
			nuc990_i2s_write_reg(info, NUC990_I2S_RSR, val);
		}

	} else if (val & GLBCON_P_DMA_IRQ) {
		stream = SNDRV_PCM_STREAM_PLAYBACK;
		nuc990_i2s_write_reg(info, NUC990_I2S_CON,
				     val | GLBCON_P_DMA_IRQ);

		val = nuc990_i2s_read_reg(info, NUC990_I2S_PSR);

		if (val & PSR_P_DMA_RIA_IRQ) {
			val = PSR_P_DMA_RIA_IRQ;
			nuc990_i2s_write_reg(info, NUC990_I2S_PSR, val);
		}

	} else {
		dev_err(info->dev, "Wrong DMA interrupt status!\n");
		spin_unlock_irqrestore(&info->irqlock, flags);
		return IRQ_HANDLED;
	}

	spin_unlock_irqrestore(&info->irqlock, flags);

	snd_pcm_period_elapsed(info->substream[stream]);

	return IRQ_HANDLED;
}

static int nuc990_dma_hw_free(struct snd_soc_component *component,
			      struct snd_pcm_substream *substream)
{
	struct snd_pcm_runtime *runtime = substream->runtime;
	struct nuc990_i2s_info *info = runtime->private_data;

	snd_pcm_lib_free_pages(substream);
	info->substream[substream->stream] = NULL;
	return 0;
}

static int nuc990_dma_prepare(struct snd_soc_component *component,
			      struct snd_pcm_substream *substream)
{
	struct snd_pcm_runtime *runtime = substream->runtime;
	struct nuc990_i2s_info *info = runtime->private_data;
	unsigned long flags, val;
	int ret = 0;

	spin_lock_irqsave(&info->irqlock, flags);

	nuc990_update_dma_register(substream, info->dma_addr[substream->stream],
				   info->buffersize[substream->stream]);

	val = nuc990_i2s_read_reg(info, NUC990_I2S_RESET);

	switch (runtime->channels) {
	case 1:
		if (substream->stream == SNDRV_PCM_STREAM_PLAYBACK) {
			val = (val & ~(RESET_PLAY_SINGLE_MASK)) |
			      RESET_PLAY_SINGLE_MONO;
		} else {
			val = (val & ~(RESET_RECORD_SINGLE_MASK)) |
			      RESET_RECORD_SINGLE_LEFT;
		}
		nuc990_i2s_write_reg(info, NUC990_I2S_RESET, val);
		break;
	case 2:
		if (substream->stream == SNDRV_PCM_STREAM_PLAYBACK)
			val |= RESET_PLAY_SINGLE_STEREO;
		else
			val |= RESET_RECORD_SINGLE_DUAL;
		nuc990_i2s_write_reg(info, NUC990_I2S_RESET, val);
		break;
	default:
		ret = -EINVAL;
	}

	/* set DMA IRQ to half */
	val = nuc990_i2s_read_reg(info, NUC990_I2S_CON);
	val = val & ~(GLBCON_P_DMA_IRQ_SEL_MASK | GLBCON_R_DMA_IRQ_SEL_MASK);
	val |= (GLBCON_R_DMA_IRQ_SEL_HALF | GLBCON_P_DMA_IRQ_SEL_HALF);
	nuc990_i2s_write_reg(info, NUC990_I2S_CON, val);

	spin_unlock_irqrestore(&info->irqlock, flags);
	return ret;
}

static int nuc990_dma_getposition(struct snd_pcm_substream *substream,
				  dma_addr_t *src, dma_addr_t *dst)
{
	struct snd_pcm_runtime *runtime = substream->runtime;
	struct nuc990_i2s_info *info = runtime->private_data;

	if (src != NULL)
		*src = nuc990_i2s_read_reg(info, NUC990_I2S_PDESC);

	if (dst != NULL)
		*dst = nuc990_i2s_read_reg(info, NUC990_I2S_RDESC);

	return 0;
}

static snd_pcm_uframes_t nuc990_dma_pointer(struct snd_soc_component *component,
					    struct snd_pcm_substream *substream)
{
	struct snd_pcm_runtime *runtime = substream->runtime;
	dma_addr_t src, dst;
	unsigned long res;
	struct nuc990_i2s_info *info = runtime->private_data;
	snd_pcm_uframes_t frames;

	spin_lock(&info->lock);

	nuc990_dma_getposition(substream, &src, &dst);

	if (substream->stream == SNDRV_PCM_STREAM_CAPTURE)
		res = dst - runtime->dma_addr;
	else
		res = src - runtime->dma_addr;

	frames = bytes_to_frames(substream->runtime, res);

	spin_unlock(&info->lock);

	return frames;
}

static int nuc990_dma_open(struct snd_soc_component *component,
			   struct snd_pcm_substream *substream)
{
	struct snd_soc_pcm_runtime *rtd = substream->private_data;
	struct snd_soc_dai *cpu_dai = asoc_rtd_to_cpu(rtd, 0);
	struct nuc990_i2s_info *info = snd_soc_dai_get_drvdata(cpu_dai);
	struct snd_pcm_runtime *runtime = substream->runtime;

	snd_soc_set_runtime_hwparams(substream, &nuc990_pcm_hardware);
	runtime->private_data = info;

	return 0;
}

static int nuc990_dma_close(struct snd_soc_component *component,
			    struct snd_pcm_substream *substream)
{
	return 0;
}

static int nuc990_dma_mmap(struct snd_soc_component *component,
			   struct snd_pcm_substream *substream,
			   struct vm_area_struct *vma)
{
	vma->vm_page_prot = pgprot_writecombine(vma->vm_page_prot);

	return remap_pfn_range(vma, vma->vm_start,
			       substream->runtime->dma_addr >> PAGE_SHIFT,
			       vma->vm_end - vma->vm_start, vma->vm_page_prot);
}

static void nuc990_dma_free_dma_buffers(struct snd_soc_component *component,
					struct snd_pcm *pcm)
{
	snd_pcm_lib_preallocate_free_for_all(pcm);
}

static u64 nuc990_pcm_dmamask = DMA_BIT_MASK(32);
static int nuc990_dma_new(struct snd_soc_component *component,
			  struct snd_soc_pcm_runtime *rtd)
{
	struct snd_card *card = rtd->card->snd_card;
	struct snd_pcm *pcm = rtd->pcm;

	if (!card->dev->dma_mask)
		card->dev->dma_mask = &nuc990_pcm_dmamask;
	if (!card->dev->coherent_dma_mask)
		card->dev->coherent_dma_mask = DMA_BIT_MASK(32);

	snd_pcm_lib_preallocate_pages_for_all(
		pcm, SNDRV_DMA_TYPE_DEV, card->dev, 4 * 1024, (4 * 1024) - 1);

	return 0;
}

static const struct snd_soc_component_driver nuc990_audio_component = {
	.name = "nuc990-audio",
	.pcm_construct = nuc990_dma_new,
	.pcm_destruct = nuc990_dma_free_dma_buffers,
	.open = nuc990_dma_open,
	.close = nuc990_dma_close,
	.hw_params = nuc990_dma_hw_params,
	.hw_free = nuc990_dma_hw_free,
	.prepare = nuc990_dma_prepare,
	.pointer = nuc990_dma_pointer,
	.mmap = nuc990_dma_mmap,

};

static int nuc990_i2s_runtime_suspend(struct device *dev)
{
	struct nuc990_i2s_info *info = dev_get_drvdata(dev);

	nuc990_i2s_disable(info);
	return 0;
}

static int nuc990_i2s_runtime_resume(struct device *dev)
{
	struct nuc990_i2s_info *info = dev_get_drvdata(dev);

	nuc990_i2s_enable(info);

	return 0;
}

static void nuc990_i2s_drvremove(struct platform_device *pdev)
{
	if (!pm_runtime_status_suspended(&pdev->dev))
		nuc990_i2s_runtime_suspend(&pdev->dev);
}

static int nuc990_i2s_drvprobe(struct platform_device *pdev)
{
	struct nuc990_i2s_info *info;
	int ret;

	//printk("Enter %s.....\n", __FUNCTION__);

	info = devm_kzalloc(&pdev->dev, sizeof(struct nuc990_i2s_info),
			    GFP_KERNEL);
	if (!info)
		return -ENOMEM;

	info->dev = &pdev->dev;
	platform_set_drvdata(pdev, info);
	spin_lock_init(&info->lock);
	spin_lock_init(&info->irqlock);

	info->mmio =
		devm_platform_get_and_ioremap_resource(pdev, 0, &info->res);
	if (IS_ERR(info->mmio))
		return PTR_ERR(info->mmio);

	info->clk = devm_clk_get(&pdev->dev, NULL);
	if (IS_ERR(info->clk)) {
		dev_err(&pdev->dev, "clk_get error\n");
		ret = PTR_ERR(info->clk);
		return ret;
	}

	info->irq_num = platform_get_irq(pdev, 0);
	if (!info->irq_num) {
		dev_err(&pdev->dev, "platform_get_irq error\n");
		ret = -EBUSY;
		return ret;
	}

	ret = devm_request_irq(&pdev->dev, info->irq_num, nuc990_dma_interrupt,
			       0, dev_name(&pdev->dev), info);
	if (ret)
		return -EBUSY;

	ret = devm_pm_runtime_enable(&pdev->dev);
	if (ret)
		return ret;
	if (!pm_runtime_enabled(&pdev->dev)) {
		ret = nuc990_i2s_runtime_resume(&pdev->dev);
		if (ret)
			return ret;
	}

	ret = devm_snd_soc_register_component(
		&pdev->dev, &nuc990_audio_component, &nuc990_i2s_dai, 1);
	if (ret) {
		dev_err(&pdev->dev, "failed to register ASoC DAI\n");
		return ret;
	}

	return 0;
}

static const struct of_device_id nuc990_audio_match[] = {
	{ .compatible = "nuvoton,nuc990-audio-i2s" },
	{},
};
MODULE_DEVICE_TABLE(of, nuc990_audio_match);

static const struct dev_pm_ops nuc990_i2s_pm_ops = {
	.runtime_resume = nuc990_i2s_runtime_resume,
	.runtime_suspend = nuc990_i2s_runtime_suspend,
};

static struct platform_driver nuc990_audio_driver = {
	.driver = {
		.name   = "nuc990-audio-i2s",
		.pm = &nuc990_i2s_pm_ops,
		.of_match_table = of_match_ptr(nuc990_audio_match),
	},
	.probe      = nuc990_i2s_drvprobe,
	.remove_new     = nuc990_i2s_drvremove,
};

module_platform_driver(nuc990_audio_driver);

MODULE_DESCRIPTION("NUC990 IIS SoC driver!");
MODULE_LICENSE("GPL");
MODULE_ALIAS("platform:nuc990-i2s");