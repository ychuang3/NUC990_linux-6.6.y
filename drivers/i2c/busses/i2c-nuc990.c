// SPDX-License-Identifier: GPL-2.0+
/*
 * Nuvoton NUC990 I2C driver
 *
 * Copyright (c) 2025 Nuvoton technology corporation.
 */

#include <linux/kernel.h>
#include <linux/module.h>
#include <linux/i2c.h>
#include <linux/init.h>
#include <linux/interrupt.h>
#include <linux/delay.h>
#include <linux/errno.h>
#include <linux/err.h>
#include <linux/platform_device.h>
#include <linux/clk.h>
#include <linux/slab.h>
#include <linux/io.h>
#include <linux/of.h>
#include <linux/of_device.h>
#include <linux/reset.h>
#include <linux/pm_runtime.h>

/* nuc990 i2c registers offset */

#define CTL0 0x00
#define ADDR0 0x04
#define DAT 0x08
#define STATUS0 0x0C
#define CLKDIV 0x10
#define TOCTL 0x14
#define ADDR1 0x18
#define ADDR2 0x1C
#define ADDR3 0x20
#define ADDRMSK0 0x24
#define ADDRMSK1 0x28
#define ADDRMSK2 0x2C
#define ADDRMSK3 0x30
#define WKCTL 0x3C
#define WKSTS 0x40
#define CTL1 0x44
#define STATUS1 0x48
#define TMCTL 0x4C
#define BUSCTL 0x50
#define BUSTCTL 0x54
#define BUSSTS 0x58
#define PKTSIZE 0x5C
#define PKTCRC 0x60
#define BUSTOUT 0x64
#define CLKTOUT 0x68
#define AUTOCNT 0x78

/* nuc990 i2c Status */
// Master
#define M_START 0x08 //Start
#define M_REPEAT_START 0x10 //Master Repeat Start
#define M_TRAN_ADDR_ACK 0x18 //Master Transmit Address ACK
#define M_TRAN_ADDR_NACK 0x20 //Master Transmit Address NACK
#define M_TRAN_DATA_ACK 0x28 //Master Transmit Data ACK
#define M_TRAN_DATA_NACK 0x30 //Master Transmit Data NACK
#define M_ARB_LOST 0x38 //Master Arbitration Los
#define M_RECE_ADDR_ACK 0x40 //Master Receive Address ACK
#define M_RECE_ADDR_NACK 0x48 //Master Receive Address NACK
#define M_RECE_DATA_ACK 0x50 //Master Receive Data ACK
#define M_RECE_DATA_NACK 0x58 //Master Receive Data NACK
#define BUS_ERROR 0x00 //Bus error

// Slave
#define S_REPEAT_START_STOP 0xA0 //Slave Transmit Repeat Start or Stop
#define S_TRAN_ADDR_ACK 0xA8 //Slave Transmit Address ACK
#define S_TRAN_DATA_ACK 0xB8 //Slave Transmit Data ACK
#define S_TRAN_DATA_NACK 0xC0 //Slave Transmit Data NACK
#define S_TRAN_LAST_DATA_ACK 0xC8 //Slave Transmit Last Data ACK
#define S_RECE_ADDR_ACK 0x60 //Slave Receive Address ACK
#define S_RECE_ARB_LOST 0x68 //Slave Receive Arbitration Lost
#define S_RECE_DATA_ACK 0x80 //Slave Receive Data ACK
#define S_RECE_DATA_NACK 0x88 //Slave Receive Data NACK

//GC Mode
#define GC_ADDR_ACK 0x70 //GC mode Address ACK
#define GC_ARB_LOST 0x78 //GC mode Arbitration Lost
#define GC_DATA_ACK 0x90 //GC mode Data ACK
#define GC_DATA_NACK 0x98 //GC mode Data NACK

//Other
#define ADDR_TRAN_ARB_LOST 0xB0 //Address Transmit Arbitration Lost
#define BUS_RELEASED 0xF8 //Bus Released

/*------------------------------- */
/*  I2C_CTL constant definitions. */
/*--------------------------------*/
#define I2C_CTL_STA_SI 0x28UL
#define I2C_CTL_STA_SI_AA 0x2CUL
#define I2C_CTL_STO_SI 0x18UL
#define I2C_CTL_STO_SI_AA 0x1CUL
#define I2C_CTL_SI 0x08UL
#define I2C_CTL_SI_AA 0x0CUL
#define I2C_CTL_STA 0x20UL
#define I2C_CTL_STO 0x10UL
#define I2C_CTL_AA 0x04UL

#define I2C_GCMODE_ENABLE 1
#define I2C_GCMODE_DISABLE 0

#define STOP_TIMEOUT_MS 50

/* i2c controller private data */

struct nuc990_i2c {
	spinlock_t lock;
	wait_queue_head_t wait;
	struct i2c_msg *msg;
	unsigned int msg_num;
	unsigned int msg_idx;
	unsigned int msg_ptr;
	unsigned int irq;
	unsigned int arblost;
	unsigned int i2c_port;
	void __iomem *regs;
	struct clk *clk;
	struct device *dev;
	struct resource *ioarea;
	struct i2c_adapter adap;
	int stop_ret;
	struct i2c_client *slave;
	bool slave_mode;
	struct reset_control *rst;
};

/* nuc990_i2c_master_complete
 *
 * complete the message and wake up the caller,
 * using the given return code,
 * or zero to mean ok.
 */
static inline void nuc990_i2c_master_complete(struct nuc990_i2c *i2c, int ret)
{
	dev_dbg(i2c->dev, "master_complete %d\n", ret);

	i2c->msg_ptr = 0;
	i2c->msg = NULL;
	i2c->msg_idx++;
	i2c->msg_num = 0;
	if (ret)
		i2c->msg_idx = ret;

	wake_up(&i2c->wait);
}

/* irq enable/disable functions */

static inline void nuc990_i2c_disable_irq(struct nuc990_i2c *i2c)
{
	unsigned long tmp;

	tmp = readl(i2c->regs + CTL0);
	writel(tmp & ~(0x1 << 7), i2c->regs + CTL0);
}

static inline void nuc990_i2c_enable_irq(struct nuc990_i2c *i2c)
{
	unsigned long tmp;

	tmp = readl(i2c->regs + CTL0);
	writel(tmp | (0x1 << 7), i2c->regs + CTL0);
}

/* nuc990_i2c_message_start
 *
 * put the start of a message onto the bus
 */
static void nuc990_i2c_message_start(struct nuc990_i2c *i2c)
{
	writel(((readl(i2c->regs + CTL0) & ~(I2C_CTL_STA_SI_AA | I2C_CTL_STO)) |
		I2C_CTL_SI),
	       i2c->regs + CTL0);
	writel(((readl(i2c->regs + CTL0) & ~(I2C_CTL_STA_SI_AA | I2C_CTL_STO)) |
		I2C_CTL_STA),
	       i2c->regs + CTL0);
}

static inline void nuc990_i2c_stop(struct nuc990_i2c *i2c, int ret)
{
	unsigned int i = 0;

	dev_dbg(i2c->dev, "STOP\n");

	i2c->stop_ret = ret;

	if (readl(i2c->regs + CTL0) & I2C_CTL_AA) {
		writel((readl(i2c->regs + CTL0) & ~(I2C_CTL_AA)),
		       (i2c->regs + CTL0));
		while (((readl(i2c->regs + CTL0) & I2C_CTL_AA)) && (i < 100))
			i++;
	}

	writel(((readl(i2c->regs + CTL0) & ~(I2C_CTL_STA_SI_AA | I2C_CTL_STO)) |
		(I2C_CTL_STO | I2C_CTL_SI)),
	       (i2c->regs + CTL0));

	for (i = 0; i < STOP_TIMEOUT_MS; i++) {
		if (!(readl(i2c->regs + CTL0) & I2C_CTL_STO))
			break;

		udelay(1000);
	}

	if (i >= STOP_TIMEOUT_MS) {
		dev_dbg(i2c->dev, "I2C Stop Timeout\n");
		writel(I2C_CTL_STO_SI, (i2c->regs + CTL0));
		writel(I2C_CTL_SI, (i2c->regs + CTL0));
	}

	if (i2c->slave_mode)
		writel(((readl(i2c->regs + CTL0) &
			 ~(I2C_CTL_STA_SI_AA | I2C_CTL_STO)) |
			(I2C_CTL_SI | I2C_CTL_AA)),
		       (i2c->regs + CTL0));

	nuc990_i2c_master_complete(i2c, ret);
}

/* is_lastmsg()
 *
 * returns TRUE if the current message is the last in the set
 */
static inline int is_lastmsg(struct nuc990_i2c *i2c)
{
	return i2c->msg_idx >= (i2c->msg_num - 1);
}

/* is_msglast
 *
 * returns TRUE if we this is the last byte in the current message
 */
static inline int is_msglast(struct nuc990_i2c *i2c)
{
	return i2c->msg_ptr == i2c->msg->len - 1;
}

/* is_msgend
 *
 * returns TRUE if we reached the end of the current message
 */
static inline int is_msgend(struct nuc990_i2c *i2c)
{
	return i2c->msg_ptr >= i2c->msg->len;
}

static void I2C_SlaveTRx(struct nuc990_i2c *i2c, unsigned long iicstat)
{
	unsigned char byte;

	if (iicstat == S_RECE_ADDR_ACK) {
		/* Own SLA+W has been receive; ACK has been return */
		writel(((readl(i2c->regs + CTL0) &
			 ~(I2C_CTL_STA_SI_AA | I2C_CTL_STO)) |
			(I2C_CTL_SI | I2C_CTL_AA)),
		       (i2c->regs + CTL0));
	} else if (iicstat == S_RECE_DATA_ACK) {
		/* Previously address with own SLA address Data has been received;
		 * ACK has been returned
		 */
		byte = readb(i2c->regs + DAT);

		i2c_slave_event(i2c->slave, I2C_SLAVE_WRITE_RECEIVED, &byte);

		writel(((readl(i2c->regs + CTL0) &
			 ~(I2C_CTL_STA_SI_AA | I2C_CTL_STO)) |
			(I2C_CTL_SI | I2C_CTL_AA)),
		       (i2c->regs + CTL0));
	} else if (iicstat == S_TRAN_ADDR_ACK) {
		/* Own SLA+R has been receive; ACK has been return */
		i2c_slave_event(i2c->slave, I2C_SLAVE_READ_PROCESSED, &byte);

		writel(byte, i2c->regs + DAT);

		writel(((readl(i2c->regs + CTL0) &
			 ~(I2C_CTL_STA_SI_AA | I2C_CTL_STO)) |
			(I2C_CTL_SI | I2C_CTL_AA)),
		       (i2c->regs + CTL0));
	} else if (iicstat == S_TRAN_DATA_NACK) {
		/* Data byte or last data in I2CDAT has been transmitted.
		 * Not ACK has been received
		 */
		writel(((readl(i2c->regs + CTL0) &
			 ~(I2C_CTL_STA_SI_AA | I2C_CTL_STO)) |
			(I2C_CTL_SI | I2C_CTL_AA)),
		       (i2c->regs + CTL0));
	} else if (iicstat == S_RECE_DATA_NACK) {
		/* Previously addressed with own SLA address;
		 *	NOT ACK has been returned
		 */
		writel(((readl(i2c->regs + CTL0) &
			 ~(I2C_CTL_STA_SI_AA | I2C_CTL_STO)) |
			(I2C_CTL_SI | I2C_CTL_AA)),
		       (i2c->regs + CTL0));
	} else if (iicstat == S_REPEAT_START_STOP) {
		/* A STOP or repeated START has been received
		 *	while still addressed as Slave/Receiver
		 */
		i2c_slave_event(i2c->slave, I2C_SLAVE_STOP, &byte);

		writel(((readl(i2c->regs + CTL0) &
			 ~(I2C_CTL_STA_SI_AA | I2C_CTL_STO)) |
			(I2C_CTL_SI | I2C_CTL_AA)),
		       (i2c->regs + CTL0));
	} else {
		dev_err(i2c->dev, "Status is NOT processed\n");
		writel(((readl(i2c->regs + CTL0) & ~(0x3C)) |
			(I2C_CTL_SI | I2C_CTL_AA)),
		       (i2c->regs + CTL0));
	}
}

static void i2c_nuc990_irq_master_TRx(struct nuc990_i2c *i2c,
				      unsigned long iicstat)
{
	unsigned char byte;

	if (iicstat == M_START) {
		/* START has been transmitted and prepare SLA+W */

		if (i2c->msg->flags & I2C_M_RD)
			/* Write SLA+R to Register I2CDAT */
			writel((((i2c->msg->addr & 0x7f) << 1) | 0x1),
			       (i2c->regs + DAT));
		else
			/* Write SLA+W to Register I2CDAT */
			writel(((i2c->msg->addr & 0x7f) << 1),
			       (i2c->regs + DAT));

		writel(((readl(i2c->regs + CTL0) &
			 ~(I2C_CTL_STA_SI_AA | I2C_CTL_STO)) |
			I2C_CTL_SI),
		       (i2c->regs + CTL0));
	} else if ((iicstat == M_TRAN_ADDR_ACK) ||
		   (iicstat == M_TRAN_DATA_ACK)) {
		/* SLA+W has been transmitted and ACK has been received */

		if (iicstat == M_TRAN_ADDR_ACK) {
			if (is_lastmsg(i2c) && i2c->msg->len == 0) {
				nuc990_i2c_stop(i2c, 0);
				return;
			}
		}

		if (!is_msgend(i2c)) {
			byte = i2c->msg->buf[i2c->msg_ptr++];
			writel(byte, i2c->regs + DAT);
			writel(((readl(i2c->regs + CTL0) &
				 ~(I2C_CTL_STA_SI_AA | I2C_CTL_STO)) |
				I2C_CTL_SI),
			       (i2c->regs + CTL0));
		} else if (!is_lastmsg(i2c)) {
			/* we need to go to the next i2c message */
			dev_dbg(i2c->dev, "WRITE: Next Message\n");

			i2c->msg_ptr = 0;
			i2c->msg_idx++;
			i2c->msg++;

			/* send the new start */
			writel(((readl(i2c->regs + CTL0) &
				 ~(I2C_CTL_STA_SI_AA | I2C_CTL_STO)) |
				I2C_CTL_STA | I2C_CTL_SI),
			       (i2c->regs + CTL0));
		} else {
			/* send stop */
			nuc990_i2c_stop(i2c, 0);
		}
	} else if (iicstat == M_TRAN_DATA_NACK)
		nuc990_i2c_stop(i2c, 0);
	else if ((iicstat == M_TRAN_ADDR_NACK) ||
		 (iicstat == M_RECE_ADDR_NACK)) {
		/* Master Transmit Address NACK */
		/* 0x20: SLA+W has been transmitted and NACK has been received */
		/* 0x48: SLA+R has been transmitted and NACK has been received */

		if (!(i2c->msg->flags & I2C_M_IGNORE_NAK)) {
			dev_err(i2c->dev, "\n i2c: ack was not received\n");
			nuc990_i2c_stop(i2c, -ENXIO);
		}
	} else if (iicstat == M_REPEAT_START) {
		/* Repeat START has been transmitted and prepare SLA+R */

		if (i2c->msg->flags & I2C_M_RD)
			/* Write SLA+R to Register I2CDAT */
			writel((((i2c->msg->addr & 0x7f) << 1) | 0x1),
			       (i2c->regs + DAT));
		else
			/* Write SLA+W to Register I2CDAT */
			writel(((i2c->msg->addr & 0x7f) << 1),
			       (i2c->regs + DAT));

		writel(((readl(i2c->regs + CTL0) &
			 ~(I2C_CTL_STA_SI_AA | I2C_CTL_STO)) |
			I2C_CTL_SI),
		       (i2c->regs + CTL0));
	} else if (iicstat == M_RECE_ADDR_ACK) {
		/* SLA+R has been transmitted and ACK has been received */

		if (is_lastmsg(i2c) && i2c->msg->len == 0)
			nuc990_i2c_stop(i2c, 0);
		else if (is_lastmsg(i2c) && (i2c->msg->len == 1))
			writel(((readl(i2c->regs + CTL0) &
				 ~(I2C_CTL_STA_SI_AA | I2C_CTL_STO)) |
				I2C_CTL_SI),
			       (i2c->regs + CTL0));
		else
			writel(((readl(i2c->regs + CTL0) &
				 ~(I2C_CTL_STA_SI_AA | I2C_CTL_STO)) |
				I2C_CTL_SI_AA),
			       (i2c->regs + CTL0));
	} else if ((iicstat == M_RECE_DATA_ACK) ||
		   (iicstat == M_RECE_DATA_NACK)) {
		/* DATA has been transmitted and ACK has been received */
		byte = readb(i2c->regs + DAT);
		i2c->msg->buf[i2c->msg_ptr++] = byte;

		if (is_msglast(i2c)) {
			/* last byte of buffer */
			writel(((readl(i2c->regs + CTL0) &
				 ~(I2C_CTL_STA_SI_AA | I2C_CTL_STO)) |
				I2C_CTL_SI),
			       (i2c->regs + CTL0));
		} else if (is_msgend(i2c)) {
			/* ok, we've read the entire buffer, see if there
			 * is anything else we need to do
			 */

			if (is_lastmsg(i2c)) {
				/* last message, send stop and complete */
				dev_dbg(i2c->dev, "READ: Send Stop\n");

				nuc990_i2c_stop(i2c, 0);
			} else {
				/* go to the next transfer */
				dev_dbg(i2c->dev, "READ: Next Transfer\n");

				i2c->msg_ptr = 0;
				i2c->msg_idx++;
				i2c->msg++;

				/* send the new start */
				writel(((readl(i2c->regs + CTL0) &
					 ~(I2C_CTL_STA_SI_AA | I2C_CTL_STO)) |
					I2C_CTL_STA | I2C_CTL_SI),
				       (i2c->regs + CTL0));
			}
		} else {
			writel(((readl(i2c->regs + CTL0) &
				 ~(I2C_CTL_STA_SI_AA | I2C_CTL_STO)) |
				I2C_CTL_SI_AA),
			       (i2c->regs + CTL0));
		}

	} else {
		dev_err(i2c->dev, "Status is NOT processed\n");
		nuc990_i2c_disable_irq(i2c);
		nuc990_i2c_stop(i2c, 0);
	}
}

/* nuc990_i2c_irq
 *
 * top level IRQ servicing routine
 */
static irqreturn_t nuc990_i2c_irq(int irqno, void *dev_id)
{
	struct nuc990_i2c *i2c = dev_id;
	unsigned long status;

	status = readl(i2c->regs + STATUS0);

	if (status == M_ARB_LOST) {
		/* deal with arbitration loss */
		dev_err(i2c->dev, "deal with arbitration loss\n");
		i2c->arblost = 1;

		nuc990_i2c_disable_irq(i2c);
		nuc990_i2c_stop(i2c, 0);
		goto out;
	}

	if (status == BUS_ERROR) {
		dev_err(i2c->dev, "IRQ: error i2c->state == IDLE\n");
		nuc990_i2c_disable_irq(i2c);
		nuc990_i2c_stop(i2c, 0);
		goto out;
	}

	/* pretty much this leaves us with the fact that we've
	 * transmitted or received whatever byte we last sent
	 */

	if (i2c->slave_mode)
		I2C_SlaveTRx(i2c, status);
	else
		i2c_nuc990_irq_master_TRx(i2c, status);

out:
	return IRQ_HANDLED;
}

/* nuc990_i2c_doxfer
 *
 * this starts an i2c transfer
 */
static int nuc990_i2c_doxfer(struct nuc990_i2c *i2c, struct i2c_msg *msgs,
			     int num)
{
	unsigned long iicstat, timeout;
	int spins = 20;
	int ret;

	spin_lock_irq(&i2c->lock);

	nuc990_i2c_enable_irq(i2c);

	i2c->msg = msgs;
	i2c->msg_num = num;
	i2c->msg_ptr = 0;
	i2c->msg_idx = 0;

	nuc990_i2c_message_start(i2c);
	spin_unlock_irq(&i2c->lock);

	timeout = wait_event_timeout(i2c->wait, i2c->msg_num == 0, HZ * 5);
	ret = i2c->msg_idx;

	/* having these next two as dev_err() makes life very
	 * noisy when doing an i2cdetect
	 */

	if (timeout == 0)
		dev_dbg(i2c->dev, "timeout\n");
	else if (ret != num)
		dev_dbg(i2c->dev, "incomplete xfer (%d)\n", ret);

	nuc990_i2c_disable_irq(i2c);

	/* ensure the stop has been through the bus */
	dev_dbg(i2c->dev, "waiting for bus idle\n");

	/* first, try busy waiting briefly */
	do {
		// chekc stop bit auto clear
		iicstat = readl(i2c->regs + CTL0);
	} while ((iicstat & (0x1 << 4)) && --spins);

	/* if that timed out sleep */
	if (!spins) {
		msleep(20);
		iicstat = readl(i2c->regs + CTL0);
	}

	if (iicstat & (0x1 << 4))
		dev_warn(i2c->dev, "timeout waiting for bus idle\n");

	if (i2c->arblost) {
		dev_dbg(i2c->dev, "arb lost, stop\n");
		i2c->arblost = 0;
		nuc990_i2c_stop(i2c, 0);
		msleep(20);
		nuc990_i2c_disable_irq(i2c);
		ret = -EAGAIN;
	}

	return ret;
}

/* nuc990_i2c_xfer
 *
 * first port of call from the i2c bus code when an message needs
 * transferring across the i2c bus.
 */
static int nuc990_i2c_xfer(struct i2c_adapter *adap, struct i2c_msg *msgs,
			   int num)
{
	struct nuc990_i2c *i2c = (struct nuc990_i2c *)adap->algo_data;
	int retry;
	int ret;

	for (retry = 0; retry < adap->retries; retry++) {
		ret = nuc990_i2c_doxfer(i2c, msgs, num);

		if (ret != -EAGAIN)
			return ret;

		dev_dbg(i2c->dev, "Retrying transmission (%d)\n", retry);

		udelay(100);
	}

	return -EREMOTEIO;
}

static int nuc990_reg_slave(struct i2c_client *slave)
{
	struct nuc990_i2c *priv = i2c_get_adapdata(slave->adapter);

	if (priv->slave)
		return -EBUSY;

	if (slave->flags & I2C_CLIENT_TEN)
		return -EAFNOSUPPORT;

	nuc990_i2c_enable_irq(priv);

	pm_runtime_get_sync(priv->dev);

	priv->slave = slave;

	// Enable I2C
	writel(readl(priv->regs + CTL0) | (0x1 << 6), (priv->regs + CTL0));

	// Set Slave Address
	writel(slave->addr, (priv->regs + ADDR0));

	// I2C enter SLV mode
	writel((readl(priv->regs + CTL0) | I2C_CTL_AA | I2C_CTL_SI),
	       (priv->regs + CTL0));

	return 0;
}

static int nuc990_unreg_slave(struct i2c_client *slave)
{
	struct nuc990_i2c *priv = i2c_get_adapdata(slave->adapter);

	/* Disable I2C */
	writel(readl(priv->regs + CTL0) & ~(0x1 << 6), (priv->regs + CTL0));
	/* Disable i2c interrupt */
	nuc990_i2c_disable_irq(priv);

	priv->slave = NULL;

	pm_runtime_put_sync(priv->dev);

	return 0;
}

/* declare our i2c functionality */
static u32 nuc990_i2c_func(struct i2c_adapter *adap)
{
	return I2C_FUNC_I2C | I2C_FUNC_PROTOCOL_MANGLING | I2C_FUNC_SMBUS_EMUL;
}

/* i2c bus registration info */

static const struct i2c_algorithm nuc990_i2c_algorithm = {
	.master_xfer = nuc990_i2c_xfer,
	.functionality = nuc990_i2c_func,
	.reg_slave = nuc990_reg_slave,
	.unreg_slave = nuc990_unreg_slave,
};

/* nuc990_i2c_probe
 *
 * called by the bus driver when a suitable device is found
 */
static int nuc990_i2c_probe(struct platform_device *pdev)
{
	struct nuc990_i2c *i2c;
	struct resource *res;
	int ret, err;
	int busfreq;
	struct device *dev = &pdev->dev;

	i2c = devm_kzalloc(&pdev->dev, sizeof(*i2c), GFP_KERNEL);
	if (!i2c)
		return -ENOMEM;

	strscpy(i2c->adap.name, "nuc990-i2c", sizeof(i2c->adap.name));
	i2c->adap.owner = THIS_MODULE;
	i2c->adap.algo = &nuc990_i2c_algorithm;
	i2c->adap.retries = 2;

	spin_lock_init(&i2c->lock);
	init_waitqueue_head(&i2c->wait);

	/* find the clock and enable it */
	i2c->dev = &pdev->dev;

	i2c->clk = devm_clk_get(&pdev->dev, NULL);
	if (IS_ERR(i2c->clk)) {
		err = PTR_ERR(i2c->clk);
		dev_err(&pdev->dev, "failed to get core clk: %d\n", err);
		return -ENOENT;
	}
	err = clk_prepare_enable(i2c->clk);
	if (err)
		return -ENOENT;

	/* map the registers */
	i2c->regs = devm_platform_get_and_ioremap_resource(pdev, 0, &res);
	if (IS_ERR(i2c->regs))
		return PTR_ERR(i2c->regs);

	i2c->i2c_port = (res->start & 0xf000) >> 12;

	/* setup info block for the i2c core */

	i2c->adap.algo_data = i2c;
	i2c->slave_mode = i2c_detect_slave_mode(&pdev->dev);

	i2c->rst = devm_reset_control_get(&pdev->dev, NULL);
	if (IS_ERR(i2c->rst))
		dev_err(i2c->dev, "Error: Missing I2C controller reset\n");

	ret = of_property_read_u32(pdev->dev.of_node, "clock-frequency", &busfreq);
	if (ret) {
		dev_err(i2c->dev, "clock-frequency not specified in DT\n");
		return ret;
	}

	// Set Clock divider
	ret = DIV_ROUND_CLOSEST(clk_get_rate(i2c->clk), busfreq * 4) - 1;

	writel(ret & 0xffff, i2c->regs + CLKDIV);

	__raw_writel((__raw_readl(i2c->regs + CTL0) | (0x1 << 6)),
		     i2c->regs + CTL0);

	/* find the IRQ for this unit (note, this relies on the init call to
	 * ensure no current IRQs pending
	 */

	i2c->irq = ret = platform_get_irq(pdev, 0);
	if (ret < 0) {
		dev_err(&pdev->dev, "cannot find IRQ\n");
		return ret;
	}

	ret = devm_request_irq(&pdev->dev, i2c->irq, nuc990_i2c_irq,
			       IRQF_SHARED, dev_name(&pdev->dev), i2c);

	if (ret != 0) {
		dev_err(&pdev->dev, "cannot claim IRQ %d\n", i2c->irq);
		return ret;
	}

	i2c_set_adapdata(&i2c->adap, i2c);
	i2c->adap.dev.of_node = pdev->dev.of_node;
	ret = i2c_add_adapter(&i2c->adap);
	if (ret) {
		dev_err(dev, "failed to add bus to i2c core: %d\n", ret);
		clk_disable_unprepare(i2c->clk);
		return ret;
	}

	pm_runtime_enable(dev);

	platform_set_drvdata(pdev, i2c);

	dev_info(&pdev->dev, "%s: nuc990 I2C adapter\n",
		 dev_name(&i2c->adap.dev));

	return 0;
}

/* nuc990_i2c_remove
 *
 * called when device is removed from the bus
 */
static int nuc990_i2c_remove(struct platform_device *pdev)
{
	struct nuc990_i2c *i2c = platform_get_drvdata(pdev);

	i2c_del_adapter(&i2c->adap);
	clk_disable_unprepare(i2c->clk);
	pm_runtime_disable(&pdev->dev);

	return 0;
}

static int nuc990_i2c_suspend(struct device *dev)
{
	struct nuc990_i2c *i2c = dev_get_drvdata(dev);

	nuc990_i2c_enable_irq(i2c);

	writel(I2C_CTL_SI_AA | readl(i2c->regs + CTL0), i2c->regs + CTL0);
	writel(0x1, i2c->regs + WKCTL);
	writel(readl(i2c->regs + WKSTS), i2c->regs + WKSTS);

	enable_irq_wake(i2c->irq);

	return 0;
}

static int nuc990_i2c_resume(struct device *dev)
{
	struct nuc990_i2c *i2c = dev_get_drvdata(dev);

	writel(0x0, i2c->regs + WKCTL);
	writel(readl(i2c->regs + WKSTS), i2c->regs + WKSTS);

	disable_irq_wake(i2c->irq);
	return 0;
}

static const struct dev_pm_ops nuc990_i2c_pmops = {
	.suspend = nuc990_i2c_suspend,
	.resume = nuc990_i2c_resume,
};

#define nuc990_i2c_PMOPS (&nuc990_i2c_pmops)

static const struct of_device_id nuc990_i2c_of_match[] = {
	{ .compatible = "nuvoton,nuc990-i2c" },
	{},
};
MODULE_DEVICE_TABLE(of, nuc990_i2c_of_match);

static struct platform_driver nuc990_i2c_driver = {
	.probe      = nuc990_i2c_probe,
	.remove     = nuc990_i2c_remove,
	.driver     = {
		.name   = "nuc990-i2c",
		.owner  = THIS_MODULE,
		.of_match_table = of_match_ptr(nuc990_i2c_of_match),
		.pm = nuc990_i2c_PMOPS,
	},
};
module_platform_driver(nuc990_i2c_driver);

MODULE_DESCRIPTION("nuc990 I2C Bus driver");
MODULE_LICENSE("GPL");
MODULE_ALIAS("platform:nuc990-i2c");
