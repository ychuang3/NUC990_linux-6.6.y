// SPDX-License-Identifier: GPL-2.0
/*
 * Nuvoton NUC990 Timer driver
 *
 * Copyright (C) 2025 Nuvoton Technology Corp.
 */

#include <linux/clk-provider.h>
#include <linux/module.h>
#include <linux/fs.h>
#include <linux/init.h>
#include <linux/ioport.h>
#include <linux/slab.h>
#include <linux/miscdevice.h>
#include <linux/device.h>
#include <linux/spinlock.h>
#include <linux/wait.h>
#include <linux/poll.h>
#include <linux/sched.h>
#include <linux/interrupt.h>
#include <linux/clk.h>
#include <linux/of.h>
#include <linux/of_irq.h>
#include <linux/of_address.h>
#include <linux/of_platform.h>
#include <linux/platform_device.h>
#include <linux/pinctrl/consumer.h>

#include "regs-nuc990-timer.h"
#include <uapi/misc/nuc990_timer.h>

#define TIMER_CH 4
#define TIMER_OPMODE_NONE 0
#define TIMER_OPMODE_ONESHOT 1
#define TIMER_OPMODE_PERIODIC 2
#define TIMER_OPMODE_CONTINUOUS 3
#define TIMER_OPMODE_TOGGLE 4
#define TIMER_OPMODE_TRIGGER_COUNTING 5
#define TIMER_OPMODE_FREE_COUNTING 6
#define TIMER_OPMODE_EVENT_COUNTING 7

/* Register bit field */
#define TIMER_CNT_IEN (0x1 << 29)
#define TIMER_CNT_EN (0x1 << 30)
#define TIMER_ONESHOT_MODE (0x0 << 27)
#define TIMER_PERIODIC_MODE (0x1 << 27)
#define TIMER_TOGGLE_MODE (0x2 << 27)
#define TIMER_CONTINUOUS_MODE (0x3 << 27)
#define TIMER_EVENT_COUNTING_MODE (0x1 << 24)
#define TIMER_CAPTURE_EN (0x1 << 3)
#define TIMER_CAPTURE_FREE_COUNTING (0x0 << 4)
#define TIMER_CAPTURE_COUNTER_RESET (0x1 << 4)
#define TIMER_CAPTURE_IEN (0x1 << 5)

#define TIMER_COUNTER_RESET (TIMER_CAPTURE_COUNTER_RESET | TIMER_CAPTURE_EN)
#define TIMER_FREE_COUNTING (TIMER_CAPTURE_FREE_COUNTING | TIMER_CAPTURE_EN)

#define TIMER_PERIODIC (TIMER_PERIODIC_MODE | TIMER_CNT_EN)
#define TIMER_TOGGLE (TIMER_TOGGLE_MODE | TIMER_CNT_EN)

#define TIMER_EVENT_COUNTER (TIMER_EVENT_COUNTING_MODE | TIMER_CNT_EN)

struct nuc990_timer {
	spinlock_t lock;
	struct device *dev;
	struct clk *clk;
	struct clk *eclk;
	void __iomem *clkbase;
	void __iomem *regs;
	wait_queue_head_t wq;
	int minor; // dynamic minor num, so we need this to distinguish between channels
	u32 cap; // latest capture data
	u32 cnt; // latest timer up-counter value
	int irq; // interrupt number
	u8 ch; // timer channel. 0~3
	u8 mode; // Current OP mode. Counter, free counting, trigger counting...
	u8 occupied; // device opened
	u8 update; // new capture data available
	u8 clksel;
	u32 psc;
};

static struct nuc990_timer *tmr[TIMER_CH];

static uint32_t gu32_cnt;

static irqreturn_t nuc990_timer_interrupt(int irq, void *dev_id)
{
	struct nuc990_timer *t = (struct nuc990_timer *)dev_id;
	static int cnt;
	static uint32_t t0, t1;
	unsigned long flag = 0;

	cnt = 0;
	spin_lock(&t->lock);
	flag = __raw_readl(t->regs + REG_TIMER_INTSTS);
	if (flag & 0x1) {
		t->cnt = gu32_cnt++;
		// Clear Timer Time-out Interrupt Status
		__raw_writel(__raw_readl(t->regs + REG_TIMER_INTSTS) & 0x1,
			     t->regs + REG_TIMER_INTSTS);
		t->update = 1;
	}

	flag = __raw_readl(t->regs + REG_TIMER_EINTSTS);
	if (flag & 0x1) {
		if (t->mode == TIMER_OPMODE_FREE_COUNTING) {
			if (cnt == 0) {
				/* Gets the Timer capture data */
				t0 = __raw_readl(t->regs + REG_TIMER_CAP);
				cnt++;

			} else if (cnt == 1) {
				/* Gets the Timer capture data */
				t1 = __raw_readl(t->regs + REG_TIMER_CAP);
				cnt++;

				if (t0 > t1) {
					/* over run, drop this data and do nothing */

				} else {
					/* Display the measured input frequency */
					t->cap = 12000000 / (t1 - t0);
					t->update = 1;
				}
			} else {
				cnt = 0;
			}

		} else {
			t->cap = __raw_readl(t->regs + REG_TIMER_CAP);
			t->update = 1;
		}

		// Clear Timer capture Interrupt Status
		__raw_writel(__raw_readl(t->regs + REG_TIMER_EINTSTS) & 0x1,
			     t->regs + REG_TIMER_EINTSTS);
	}

	wake_up_interruptible(&t->wq);
	spin_unlock(&t->lock);

	return IRQ_HANDLED;
}

static void timer_SwitchClkSrc(u8 u8clksel, struct nuc990_timer *t)
{
	struct clk *clkmux;
	u8 ch;
	u32 val, tmpval;
	ulong tmrFreq;
	int ret;

	ch = t->ch;
	t->clksel = u8clksel;

	if (u8clksel == 0) {
		t->psc = (12 - 1);
		tmrFreq = 12000000;
	} else if (u8clksel == 1) {
		t->psc = 10 - 1;
		tmrFreq = 75000000;
	} else if (u8clksel == 2) {
		t->psc = (1 - 1);
		tmrFreq = 36621;
	} else {
		t->psc = (1 - 1);
		tmrFreq = 32768;
	}

	val = __raw_readl(t->clkbase + 0x40);
	pr_debug("   tmr%d before clksel0:0x%08x  >>>\n", ch, val);
	tmpval = (u8clksel << (ch * 2 + 16));
	__raw_writel(tmpval | (val & ~(0x3 << (ch * 2 + 16))),
		     t->clkbase + 0x40);
	val = __raw_readl(t->clkbase + 0x40);
	pr_debug("  tmr%d after  clksel0: [ 0x%08x ]  >>>\n", ch, val);

	t->eclk = of_clk_get(t->dev->of_node, 0);
	if (IS_ERR(t->eclk)) {
		ret = PTR_ERR(t->eclk);
		dev_err(t->dev, "failed to get tmr eclk, ret %d\n", ret);
		return;
	}

	clk_set_rate(t->eclk, tmrFreq);

	clkmux = clk_get_parent(t->eclk);
	if (IS_ERR(clkmux)) {
		dev_err(t->dev, "failed to get tmr eclock\n");
		ret = PTR_ERR(clkmux);
		return;
	}

	ret = clk_set_parent(t->eclk, clkmux);
	if (ret < 0) {
		dev_err(t->dev, "failed to set parent %s for %s: %d\n",
			__clk_get_name(clkmux), __clk_get_name(t->eclk), ret);
		return;
	}
}

static void stop_timer(struct nuc990_timer *t)
{
	unsigned long flag;

	spin_lock_irqsave(&t->lock, flag);
	// stop timer
	__raw_writel((__raw_readl(t->regs + REG_TIMER_CTL) & ~(1 << 30)),
		     t->regs + REG_TIMER_CTL);
	// disable interrupt
	__raw_writel((__raw_readl(t->regs + REG_TIMER_CTL) & ~(1 << 29)),
		     t->regs + REG_TIMER_CTL);
	// clear interrupt flag if any
	__raw_writel(0x1, t->regs + REG_TIMER_INTSTS);
	__raw_writel(0x1, t->regs + REG_TIMER_EINTSTS);
	t->mode = TIMER_OPMODE_NONE;
	t->update = 0;
	spin_unlock_irqrestore(&t->lock, flag);
}

static ssize_t timer_read(struct file *filp, char __user *buf, size_t count,
			  loff_t *f_pos)
{
	unsigned long flag;
	struct nuc990_timer *t = (struct nuc990_timer *)filp->private_data;
	int ret = 0;

	spin_lock_irqsave(&t->lock, flag);
	if (t->mode != TIMER_OPMODE_TRIGGER_COUNTING &&
	    t->mode != TIMER_OPMODE_FREE_COUNTING &&
	    t->mode != TIMER_OPMODE_PERIODIC &&
	    t->mode != TIMER_OPMODE_EVENT_COUNTING) {
		ret = -EPERM;

		goto out;
	}

	if (t->update) {
		if (t->mode == TIMER_OPMODE_TRIGGER_COUNTING ||
		    t->mode == TIMER_OPMODE_FREE_COUNTING) {
			if (copy_to_user(buf, &t->cap, sizeof(unsigned int)))
				ret = -EFAULT;
			else
				ret = 4; // size of int.
		} else if (t->mode == TIMER_OPMODE_PERIODIC ||
			   t->mode == TIMER_OPMODE_EVENT_COUNTING) {
			if (copy_to_user(buf, &t->cnt, sizeof(unsigned int)))
				ret = -EFAULT;
			else
				ret = 4; // size of int.
		}
		t->update = 0;

		goto out;
	} else {
		spin_unlock_irqrestore(&t->lock, flag);
		wait_event_interruptible(t->wq, t->update != 0);
		if (t->mode == TIMER_OPMODE_TRIGGER_COUNTING ||
		    t->mode == TIMER_OPMODE_FREE_COUNTING) {
			if (copy_to_user(buf, &t->cap, sizeof(unsigned int)))
				ret = -EFAULT;
			else
				ret = 4; // size of int.
		} else if (t->mode == TIMER_OPMODE_PERIODIC ||
			   t->mode == TIMER_OPMODE_EVENT_COUNTING) {
			if (copy_to_user(buf, &t->cnt, sizeof(unsigned int)))
				ret = -EFAULT;
			else
				ret = 4; // size of int.
		}
		t->update = 0;

		return ret;
	}

out:
	spin_unlock_irqrestore(&t->lock, flag);

	return ret;
}

static int timer_release(struct inode *inode, struct file *filp)
{
	struct nuc990_timer *t = (struct nuc990_timer *)filp->private_data;
	int ch = t->ch;
	unsigned long flag;

	stop_timer(t);

	// free irq
	free_irq(tmr[ch]->irq, tmr[ch]);
	// disable clk
	clk_disable_unprepare(tmr[ch]->eclk);
	clk_disable_unprepare(tmr[ch]->clk);

	spin_lock_irqsave(&tmr[ch]->lock, flag);
	tmr[ch]->occupied = 0;
	spin_unlock_irqrestore(&tmr[ch]->lock, flag);
	filp->private_data = NULL;

	return 0;
}
static int timer_open(struct inode *inode, struct file *filp)
{
	int i, ret;
	u8 ch;
	unsigned long flag;
	struct clk *clkmux;
	int minor = iminor(inode);

	for (i = 0; i < TIMER_CH; i++) {
		if (tmr[i]->minor == minor) {
			ch = i;
			break;
		}
	}

	spin_lock_irqsave(&tmr[ch]->lock, flag);
	if (tmr[ch]->occupied) {
		spin_unlock_irqrestore(&tmr[ch]->lock, flag);
		pr_debug("-EBUSY error\n");
		return -EBUSY;
	}

	tmr[ch]->occupied = 1;
	spin_unlock_irqrestore(&tmr[ch]->lock, flag);

	if (request_irq(tmr[ch]->irq, nuc990_timer_interrupt, IRQF_NO_SUSPEND,
			"nuc990-timer", tmr[ch])) {
		pr_debug("register irq failed %d\n", tmr[ch]->irq);
		ret = -EAGAIN;
		goto out2;
	}
	filp->private_data = tmr[ch];

	clkmux = clk_get_parent(tmr[ch]->eclk);
	if (IS_ERR(clkmux)) {
		dev_err(tmr[ch]->dev, "failed to get tmr eclock\n");
		ret = PTR_ERR(clkmux);
		goto out1;
	}

	ret = clk_set_parent(tmr[ch]->eclk, clkmux);
	if (ret < 0) {
		dev_err(tmr[ch]->dev, "failed to set parent %s for %s: %d\n",
			__clk_get_name(clkmux), __clk_get_name(tmr[ch]->eclk),
			ret);
		goto out1;
	}

	clk_prepare(tmr[ch]->clk);
	clk_enable(tmr[ch]->clk);
	clk_prepare(tmr[ch]->eclk);
	clk_enable(tmr[ch]->eclk);

	return 0;

out1:
	free_irq(tmr[ch]->irq, tmr[ch]);
out2:
	spin_lock_irqsave(&tmr[ch]->lock, flag);
	tmr[ch]->occupied = 0;
	spin_unlock_irqrestore(&tmr[ch]->lock, flag);

	return ret;
}

static long timer_ioctl(struct file *filp, unsigned int cmd, unsigned long arg)
{
	unsigned long flag;
	struct nuc990_timer *t = (struct nuc990_timer *)filp->private_data;
	unsigned int param;

	// stop timer before we do any change
	stop_timer(t);

	// init time-out counts
	gu32_cnt = 1;
	t->cnt = gu32_cnt;
	timer_SwitchClkSrc(t->clksel, t);
	switch (cmd) {
	case TMR_IOC_CLKLXT:
	case TMR_IOC_CLKHXT:
		if (copy_from_user((void *)&param, (const void *)arg,
				   sizeof(unsigned int)))
			return -EFAULT;
		// switch clock source
		timer_SwitchClkSrc(param, t);
		break;

	case TMR_IOC_STOP:
		//timer stopped
		break;

	case TMR_IOC_PERIODIC:
		if (copy_from_user((void *)&param, (const void *)arg,
				   sizeof(unsigned int)))
			return -EFAULT;

		// compare register is 24-bit width
		if (param > 0xFFFFFF)
			return -EPERM;

		spin_lock_irqsave(&t->lock, flag);

		// timer clock is 12MHz, set prescaler to 12 - 1.
		__raw_writel(param, t->regs + REG_TIMER_CMP);

		// enable timeout interrupt
		__raw_writel(TIMER_PERIODIC | TIMER_CNT_IEN | t->psc,
			     t->regs + REG_TIMER_CTL);

		t->mode = TIMER_OPMODE_PERIODIC;
		spin_unlock_irqrestore(&t->lock, flag);

		break;

	case TMR_IOC_TOGGLE:
		// get output duty in us
		if (copy_from_user((void *)&param, (const void *)arg,
				   sizeof(unsigned int)))
			return -EFAULT;
		// divide by 2 because a duty cycle is high + low
		param >>= 1;
		// compare register is 24-bit width
		if (param > 0xFFFFFF)
			return -EPERM;

		spin_lock_irqsave(&t->lock, flag);
		__raw_writel(param, t->regs + REG_TIMER_CMP);
		__raw_writel(TIMER_TOGGLE | t->psc, t->regs + REG_TIMER_CTL);
		t->mode = TIMER_OPMODE_TOGGLE;
		spin_unlock_irqrestore(&t->lock, flag);
		break;

	case TMR_IOC_EVENT_COUNTING:
		// get capture setting
		if (copy_from_user((void *)&param, (const void *)arg,
				   sizeof(unsigned int)))
			return -EFAULT;

		spin_lock_irqsave(&t->lock, flag);
		__raw_writel(param, t->regs + REG_TIMER_CMP);
		__raw_writel(TIMER_EVENT_COUNTER | TIMER_PERIODIC_MODE |
				     TIMER_CNT_IEN,
			     t->regs + REG_TIMER_CTL);

		t->mode = TIMER_OPMODE_EVENT_COUNTING;
		spin_unlock_irqrestore(&t->lock, flag);

		break;

	case TMR_IOC_FREE_COUNTING:
		// get capture setting
		if (copy_from_user((void *)&param, (const void *)arg,
				   sizeof(unsigned int)))
			return -EFAULT;

		spin_lock_irqsave(&t->lock, flag);
		__raw_writel(t->psc | param | TIMER_PERIODIC,
			     t->regs + REG_TIMER_CTL);
		__raw_writel(0xFFFFFF, t->regs + REG_TIMER_CMP);
		// enable capture interrupt
		__raw_writel(TIMER_CAPTURE_IEN | TIMER_FREE_COUNTING,
			     t->regs + REG_TIMER_EXTCTL);

		t->mode = TIMER_OPMODE_FREE_COUNTING;
		spin_unlock_irqrestore(&t->lock, flag);

		break;

	case TMR_IOC_TRIGGER_COUNTING:
		// get capture setting
		if (copy_from_user((void *)&param, (const void *)arg,
				   sizeof(unsigned int)))
			return -EFAULT;

		spin_lock_irqsave(&t->lock, flag);
		__raw_writel(t->psc | param | TIMER_PERIODIC,
			     t->regs + REG_TIMER_CTL);
		__raw_writel(0xFFFFFF, t->regs + REG_TIMER_CMP);
		// enable capture interrupt
		__raw_writel(TIMER_CAPTURE_IEN | TIMER_COUNTER_RESET,
			     t->regs + REG_TIMER_EXTCTL);
		t->mode = TIMER_OPMODE_TRIGGER_COUNTING;
		spin_unlock_irqrestore(&t->lock, flag);
		break;

	default:
		return -ENOTTY;
	}
	return 0;
}

static unsigned int timer_poll(struct file *filp, poll_table *wait)
{
	struct nuc990_timer *t = (struct nuc990_timer *)filp->private_data;
	unsigned int mask = 0;

	poll_wait(filp, &t->wq, wait);
	if (t->update)
		mask |= POLLIN | POLLRDNORM;
	return mask;
}

static const struct file_operations timer_fops = {
	.owner		= THIS_MODULE,
	.open		= timer_open,
	.release	= timer_release,
	.read		= timer_read,
	.unlocked_ioctl = timer_ioctl,
	.poll		= timer_poll,
};

static struct miscdevice timer_dev[] = {
	[0] = {
		.minor = MISC_DYNAMIC_MINOR,
		.name = "timer0",
		.fops = &timer_fops,
	},
	[1] = {
		.minor = MISC_DYNAMIC_MINOR,
		.name = "timer1",
		.fops = &timer_fops,
	},
	[2] = {
		.minor = MISC_DYNAMIC_MINOR,
		.name = "timer2",
		.fops = &timer_fops,
	},
	[3] = {
		.minor = MISC_DYNAMIC_MINOR,
		.name = "timer3",
		.fops = &timer_fops,
	},

};

static int nuc990_timer_probe(struct platform_device *pdev)
{
	struct device *dev = &pdev->dev;
	struct device_node *node = dev->of_node;
	int ch = 0;
	const char *clkmux;
	struct resource *r;
	u32 val32[2], val;
	int ret;

	dev_info(&pdev->dev, "NUC990 Timer\n");
	if (of_property_read_u32_array(pdev->dev.of_node, "port-number", val32,
				       1) != 0) {
		pr_err("%s can not get port-number!\n", __func__);
		return -EINVAL;
	}
	ch = val32[0];
	tmr[ch] = devm_kzalloc(&pdev->dev, sizeof(struct nuc990_timer),
			       GFP_KERNEL);
	if (tmr[ch] == NULL)
		return -ENOMEM;

	r = platform_get_resource(pdev, IORESOURCE_MEM, 0);
	tmr[ch]->regs = devm_ioremap_resource(&pdev->dev, r);
	if (IS_ERR(tmr[ch]->regs))
		return PTR_ERR(tmr[ch]->regs);

	tmr[ch]->dev = &pdev->dev;
	misc_register(&timer_dev[ch]);

	tmr[ch]->clk = devm_clk_get(&pdev->dev, NULL);
	if (IS_ERR(tmr[ch]->clk)) {
		dev_err(&pdev->dev, "failed to get timer clock\n");
		return PTR_ERR(tmr[ch]->clk);
	}

	ret = clk_prepare_enable(tmr[ch]->clk);
	if (ret) {
		dev_err(&pdev->dev, "failed to enable tmr%d clk\n", ch);
		return -ENOENT;
	}

	tmr[ch]->eclk = devm_clk_get(&pdev->dev, clkmux);
	if (IS_ERR(tmr[ch]->eclk)) {
		if (PTR_ERR(tmr[ch]->eclk) != -ENOENT)
			return PTR_ERR(tmr[ch]->eclk);

		tmr[ch]->eclk = NULL;
	}

	ret = clk_prepare_enable(tmr[ch]->eclk);
	if (ret) {
		dev_err(&pdev->dev, "Failed to enable tmr%d_eclk\n", ch);
		return ret;
	}
	// Get Timer clock source
	node = of_find_compatible_node(NULL, NULL, "nuvoton,nuc990-clk");
	if (node) {
		tmr[ch]->clkbase = of_iomap(node, 0);
		if (IS_ERR(tmr[ch]->clkbase))
			return PTR_ERR(tmr[ch]->clkbase);
		val = __raw_readl(tmr[ch]->clkbase + 0x40);
		tmr[ch]->clksel = (val & (0x3 << (ch * 2 + 16))) >>
				  (ch * 2 + 16);
	}
	tmr[ch]->minor = MINOR(timer_dev[ch].minor);
	tmr[ch]->ch = ch;
	spin_lock_init(&tmr[ch]->lock);

	tmr[ch]->irq = platform_get_irq(pdev, 0);

	init_waitqueue_head(&tmr[ch]->wq);

	platform_set_drvdata(pdev, tmr[ch]);

	return 0;
}

static int nuc990_timer_remove(struct platform_device *pdev)
{
	struct nuc990_timer *t = platform_get_drvdata(pdev);
	int ch = t->ch;

	misc_deregister(&timer_dev[ch]);

	return 0;
}

#define nuc990_timer_suspend NULL
#define nuc990_timer_resume NULL

static const struct of_device_id nuc990_timer_of_match[] = {
	{ .compatible = "nuvoton,nuc990-timer" },
	{},
};
MODULE_DEVICE_TABLE(of, nuc990_timer_of_match);

static struct platform_driver nuc990_timer_driver = {
	.driver		= {
		.owner	= THIS_MODULE,
		.name	= "nuc990-timer",
		.of_match_table = of_match_ptr(nuc990_timer_of_match),
	},
	.probe		= nuc990_timer_probe,
	.remove		= nuc990_timer_remove,
	.suspend	= nuc990_timer_suspend,
	.resume		= nuc990_timer_resume,
};
module_platform_driver(nuc990_timer_driver);

MODULE_AUTHOR("Nuvoton Technology Corp.");
MODULE_ALIAS("platform:nuc990-timer");
MODULE_LICENSE("GPL");
