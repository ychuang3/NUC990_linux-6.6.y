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
#include <linux/platform_device.h>
#include <linux/uaccess.h>

#include "regs-nuc990-timer.h"
#include <uapi/misc/nuc990_timer.h>

#define DEV_NAME_LEN 16

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
	char dev_name[DEV_NAME_LEN];
	struct miscdevice miscdev;
	struct device *dev;
	struct clk *clk;
	struct clk *eclk;
	void __iomem *regs;
	wait_queue_head_t wq;
	int minor; // dynamic minor num, so we need this to distinguish between channels
	u32 cap; // latest capture data
	u32 cnt; // latest timer up-counter value
	int irq; // interrupt number
	u8 mode; // Current OP mode. Counter, free counting, trigger counting...
	u8 occupied; // device opened
	u8 update; // new capture data available
	u8 clksel;
	u32 psc;
	int port;
};

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
		t->cnt++;
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
		__raw_writel(__raw_readl(t->regs + REG_TIMER_EINTSTS) &
				     EINTSTS_CAPIF,
			     t->regs + REG_TIMER_EINTSTS);
	}

	wake_up_interruptible(&t->wq);
	spin_unlock(&t->lock);

	return IRQ_HANDLED;
}
static int timer_SwitchClkSrc(unsigned int u8clksel, unsigned int target_hz,
			      struct nuc990_timer *t)
{
	struct clk_hw *hw;
	struct clk_hw *parent_hw;
	unsigned long parent_rate;
	int ret;

	if (u8clksel >= 4)
		return -EINVAL;

	hw = __clk_get_hw(t->eclk);
	if (!hw)
		return -EINVAL;

	parent_hw = clk_hw_get_parent_by_index(hw, u8clksel);
	if (!parent_hw) {
		dev_err(t->dev, "Parent hw not found at index %d\n", u8clksel);
		return -EINVAL;
	}

	ret = clk_set_parent(t->eclk, parent_hw->clk);
	if (ret) {
		dev_err(t->dev, "Failed to set parent: %d\n", ret);
		return ret;
	}

	parent_rate = clk_hw_get_rate(parent_hw);

	if (target_hz > 0 && parent_rate >= target_hz) {
		u32 psc_val = (parent_rate / target_hz) - 1;

		t->psc = (psc_val > 0xFF) ? 0xFF : psc_val;
	} else {
		t->psc = 0;
	}

	t->clksel = u8clksel;
	dev_info(t->dev, "Match: ParentIndex %d, Rate %lu, PSC %d\n", u8clksel,
		 parent_rate, t->psc);

	return 0;
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

out:
	spin_unlock_irqrestore(&t->lock, flag);

	return ret;
}

static int timer_release(struct inode *inode, struct file *filp)
{
	struct nuc990_timer *t = (struct nuc990_timer *)filp->private_data;
	unsigned long flag;

	stop_timer(t);

	// disable clk
	clk_disable_unprepare(t->eclk);
	clk_disable_unprepare(t->clk);

	spin_lock_irqsave(&t->lock, flag);
	t->occupied = 0;
	spin_unlock_irqrestore(&t->lock, flag);
	filp->private_data = NULL;

	return 0;
}
static int timer_open(struct inode *inode, struct file *filp)
{
	struct nuc990_timer *t;
	int ret = 0;
	unsigned long flag;

	t = container_of(filp->private_data, struct nuc990_timer, miscdev);
	filp->private_data = t;

	spin_lock_irqsave(&t->lock, flag);
	if (t->occupied) {
		spin_unlock_irqrestore(&t->lock, flag);
		pr_debug("-EBUSY error\n");
		return -EBUSY;
	}

	t->occupied = 1;
	spin_unlock_irqrestore(&t->lock, flag);

	clk_prepare_enable(t->clk);
	clk_prepare_enable(t->eclk);

	return ret;
}

static long timer_ioctl(struct file *filp, unsigned int cmd, unsigned long arg)
{
	unsigned long flag;
	struct nuc990_timer *t = (struct nuc990_timer *)filp->private_data;
	struct nuc990_timer_config config;
	unsigned int param;

	// stop timer before we do any change
	stop_timer(t);

	t->cnt = 0;
	//timer_SwitchClkSrc(t->clksel, t);
	switch (cmd) {
	case TMR_IOC_CLKSET:
		if (copy_from_user(&config, (void __user *)arg,
				   sizeof(struct nuc990_timer_config)))
			return -EFAULT;

		timer_SwitchClkSrc(config.clk_idx, config.target_hz, t);
		break;
	case TMR_IOC_CLKLXT:
	case TMR_IOC_CLKHXT:
		if (copy_from_user((void *)&param, (const void *)arg,
				   sizeof(unsigned int)))
			return -EFAULT;
		timer_SwitchClkSrc(param, 1000000, t);
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
	.owner = THIS_MODULE,
	.open = timer_open,
	.release = timer_release,
	.read = timer_read,
	.unlocked_ioctl = timer_ioctl,
	.poll = timer_poll,
};

static int nuc990_timer_probe(struct platform_device *pdev)
{
	struct device *dev = &pdev->dev;
	struct resource *res;
	struct nuc990_timer *t;
	int ret;

	t = devm_kzalloc(&pdev->dev, sizeof(struct nuc990_timer), GFP_KERNEL);
	if (t == NULL)
		return -ENOMEM;

	t->regs = devm_platform_get_and_ioremap_resource(pdev, 0, &res);
	if (IS_ERR(t->regs))
		return PTR_ERR(t->regs);

	t->dev = &pdev->dev;

	t->clk = devm_clk_get(t->dev, "timer");
	t->eclk = devm_clk_get(t->dev, "eclk");
	if (IS_ERR(t->clk) || IS_ERR(t->eclk))
		return -EPROBE_DEFER;

	spin_lock_init(&t->lock);

	t->irq = platform_get_irq(pdev, 0);

	init_waitqueue_head(&t->wq);

	platform_set_drvdata(pdev, t);

	if (devm_request_irq(t->dev, t->irq, nuc990_timer_interrupt,
			     IRQF_NO_SUSPEND, dev_name(t->dev), t)) {
		pr_debug("register irq failed %d\n", t->irq);
		return -EAGAIN;
	}

	t->port = ((res->start & BIT(12)) >> 12) * 2 +
		  ((res->start & BIT(8)) >> 8);
	snprintf(t->dev_name, DEV_NAME_LEN, "timer%d", t->port);
	t->miscdev.name = t->dev_name;
	t->miscdev.minor = MISC_DYNAMIC_MINOR;
	t->miscdev.fops = &timer_fops;
	t->miscdev.parent = dev;
	ret = misc_register(&t->miscdev);
	if (ret)
		dev_err(dev, "error:%d. Unable to register device", ret);

	dev_info(dev, "%s: nuc990 Timer\n", dev_name(t->miscdev.this_device));

	return ret;
}

static int nuc990_timer_remove(struct platform_device *pdev)
{
	struct nuc990_timer *t = platform_get_drvdata(pdev);

	misc_deregister(&t->miscdev);

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
