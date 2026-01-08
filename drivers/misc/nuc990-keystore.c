// SPDX-License-Identifier: GPL-2.0
/*
 * Nuvoton NUC990 series key store driver
 *
 * Copyright (c) 2025 Nuvoton technology corporation.
 */

#include <linux/clk.h>
#include <linux/fs.h>
#include <linux/io.h>
#include <linux/kernel.h>
#include <linux/miscdevice.h>
#include <linux/module.h>
#include <linux/of_platform.h>
#include <linux/slab.h>
#include <linux/uaccess.h>
#include <uapi/misc/nuc990_keystore.h>

#define MISCDEV_NAME		"ksdev"

#define KS_BUSY_TIMEOUT		1000

struct nuc990_ks_dev {
	struct device *dev;
	struct miscdevice miscdev;
	void __iomem *reg_base;
	struct clk *clk;
};

/*
 *  Word count of key index to key size in bit 128, 163, 192, 224,
 *  233, 255, 256, 283, 384, 409, 512, 521, 571 respectively.
 */
static const uint16_t keysz_tbl[] = {
	4, 6, 6, 7, 8, 8, 8, 9, 12, 13, 16, 17, 18, 0, 0, 0, 32, 48, 64, 96, 128
};

static inline u32  nuc990_read_reg(struct nuc990_ks_dev *ks_dev, u32 offset)
{
	u32 value = readl_relaxed(ks_dev->reg_base + offset);

	dev_vdbg(ks_dev->dev, "reg read 0x%08x from <0x%x>\n", value, offset);
	return value;
}

static inline void nuc990_write_reg(struct nuc990_ks_dev *ks_dev, u32 offset, u32 value)
{
	dev_vdbg(ks_dev->dev, "write 0x%08x into <0x%x>\n", value, offset);
	writel_relaxed(value, ks_dev->reg_base + offset);
}

static inline int nuc990_ks_wait_busy_clear(struct nuc990_ks_dev *ks_dev)
{
	unsigned long timeout = jiffies + msecs_to_jiffies(KS_BUSY_TIMEOUT);

	while (nuc990_read_reg(ks_dev, KS_STS) & KS_STS_BUSY) {
		if (time_after(jiffies, timeout)) {
			dev_err(ks_dev->dev, "KeyStore busy timeout\n");
			return -EBUSY;
		}
		cpu_relax();
	}

	return 0;
}

static int nuc990_ks_read(struct nuc990_ks_dev *ks_dev, int type, void __user *arg)
{
	struct ks_read_args r_args;
	int err, remain_cnt;
	int offset, i, cnt;
	u32 cont_msk;

	err = copy_from_user(&r_args, arg, sizeof(r_args));
	if (err)
		return -EFAULT;

	if (!r_args.word_cnt || r_args.word_cnt > ARRAY_SIZE(r_args.key))
		return -EINVAL;

	/* Just return when key store is in busy */
	if (nuc990_ks_wait_busy_clear(ks_dev) != 0)
		return -EBUSY;

	nuc990_write_reg(ks_dev, KS_METADATA, (type << KS_METADATA_DST_Pos) |
			 KS_TOMETAKEY(r_args.key_idx));

	nuc990_write_reg(ks_dev, KS_STS, KS_STS_EIF);
	offset = 0;
	cont_msk = 0;
	remain_cnt = r_args.word_cnt;

	do {
		/* Clear Status */
		nuc990_write_reg(ks_dev, KS_STS, KS_STS_EIF | KS_STS_IF);

		/* Trigger to read the key */
		nuc990_write_reg(ks_dev, KS_CTL, cont_msk | KS_OP_READ | KS_CTL_START);

		/* Waiting for key store processing */
		if (nuc990_ks_wait_busy_clear(ks_dev) != 0)
			return -EBUSY;

		/* Read the key to key buffer */
		cnt = remain_cnt;
		if (cnt > KS_KEY_REG_CNT)
			cnt = KS_KEY_REG_CNT;

		for (i = 0; i < cnt; i++)
			r_args.key[offset+i] = nuc990_read_reg(ks_dev, KS_KEY(i));

		cont_msk = KS_CTL_CONT;
		remain_cnt -= cnt;
		offset += cnt;

	} while (remain_cnt > 0);

	/* Check error flag */
	if (nuc990_read_reg(ks_dev, KS_STS) & KS_STS_EIF) {
		dev_err(ks_dev->dev, "KS EIF set on reading keys!\n");
		return -EIO;
	}

	err = copy_to_user(arg, &r_args, sizeof(r_args));
	if (err)
		err = -EFAULT;

	return err;
}

static int nuc990_ks_write(struct nuc990_ks_dev *ks_dev, int type, void __user *arg)
{
	struct ks_write_args w_args;
	int err, remain_cnt;
	int offset, i, cnt;
	u32 sidx, cont_msk;

	err = copy_from_user(&w_args, arg, sizeof(w_args));
	if (err)
		return -EFAULT;

	sidx = (w_args.meta_data & KS_METADATA_SIZE_Msk) >> KS_METADATA_SIZE_Pos;
	if (sidx >= ARRAY_SIZE(keysz_tbl))
		return -EINVAL;

	remain_cnt = keysz_tbl[sidx];

	if (w_args.word_cnt != remain_cnt)
		return -EINVAL;

	/* Just return when key store is in busy */
	if (nuc990_ks_wait_busy_clear(ks_dev) != 0)
		return -EBUSY;

	if (type == KS_OTP) {
		nuc990_write_reg(ks_dev, KS_METADATA, (KS_OTP << KS_METADATA_DST_Pos) |
				 w_args.meta_data | KS_TOMETAKEY(w_args.key_idx));

	} else {
		nuc990_write_reg(ks_dev, KS_METADATA, (KS_SRAM << KS_METADATA_DST_Pos) |
				 w_args.meta_data);
	}

	/* Clear error flag */
	nuc990_write_reg(ks_dev, KS_STS, KS_STS_EIF);
	offset = 0;
	cont_msk = 0;
	do {
		/* Prepare the key to write */
		cnt = remain_cnt;
		if (cnt > KS_KEY_REG_CNT)
			cnt = KS_KEY_REG_CNT;

		for (i = 0; i < cnt; i++)
			nuc990_write_reg(ks_dev, KS_KEY(i), w_args.key[offset + i]);

		/* Clear Status */
		nuc990_write_reg(ks_dev, KS_STS, KS_STS_EIF | KS_STS_IF);

		/* Write the key */
		nuc990_write_reg(ks_dev, KS_CTL, cont_msk | KS_OP_WRITE | KS_CTL_START);

		cont_msk = KS_CTL_CONT;
		remain_cnt -= cnt;
		offset += cnt;

		/* Waiting for key store processing */
		if (nuc990_ks_wait_busy_clear(ks_dev) != 0)
			return -EBUSY;

	} while (remain_cnt > 0);

	/* Check error flag */
	if (nuc990_read_reg(ks_dev, KS_STS) & KS_STS_EIF) {
		dev_err(ks_dev->dev, "KS EIF set on writing SRAM keys!\n");
		return -EIO;
	}

	if (type == KS_SRAM)
		return KS_TOKEYIDX(nuc990_read_reg(ks_dev, KS_METADATA));
	else
		return w_args.key_idx;
}

static int nuc990_ks_erase(struct nuc990_ks_dev *ks_dev, int type, void __user *arg)
{
	unsigned long key_idx;
	int err;

	err = copy_from_user(&key_idx, arg, sizeof(key_idx));
	if (err)
		return -EFAULT;

	if (type == KS_SRAM) {
		if (key_idx >= KS_SRAM_KEY_CNT)
			return -EINVAL;
	} else {
		if (key_idx >= KS_OTP_KEY_CNT)
			return -EINVAL;
	}


	/* Just return when key store is in busy */
	if (nuc990_ks_wait_busy_clear(ks_dev) != 0)
		return -EBUSY;

	/* Specify the key address */
	nuc990_write_reg(ks_dev, KS_METADATA, (type << KS_METADATA_DST_Pos) |
			 KS_TOMETAKEY(key_idx));

	/* Clear Status */
	nuc990_write_reg(ks_dev, KS_STS, KS_STS_EIF | KS_STS_IF);

	/* Erase the key */
	nuc990_write_reg(ks_dev, KS_CTL, KS_OP_ERASE | KS_CTL_START);

	/* Waiting for processing */
	if (nuc990_ks_wait_busy_clear(ks_dev) != 0)
		return -EBUSY;

	/* Check error flag */
	if (nuc990_read_reg(ks_dev, KS_STS) & KS_STS_EIF) {
		dev_err(ks_dev->dev, "KS EIF set on erasing a key!\n");
		return -EIO;
	}

	return 0;
}

static int nuc990_ks_erase_all(struct nuc990_ks_dev *ks_dev)
{
	/* Just return when key store is in busy */
	if (nuc990_ks_wait_busy_clear(ks_dev) != 0)
		return -EBUSY;

	/* Specify the key address */
	nuc990_write_reg(ks_dev, KS_METADATA, (KS_SRAM << KS_METADATA_DST_Pos));

	/* Clear Status */
	nuc990_write_reg(ks_dev, KS_STS, KS_STS_EIF | KS_STS_IF);

	/* Erase the key */
	nuc990_write_reg(ks_dev, KS_CTL, KS_OP_ERASE_ALL | KS_CTL_START);

	/* Waiting for processing */
	if (nuc990_ks_wait_busy_clear(ks_dev) != 0)
		return -EBUSY;

	/* Check error flag */
	if (nuc990_read_reg(ks_dev, KS_STS) & KS_STS_EIF) {
		dev_err(ks_dev->dev, "KS EIF set on erase-all!\n");
		return -EIO;
	}
	return 0;
}

static int nuc990_ks_remain(struct nuc990_ks_dev *ks_dev)
{
	return nuc990_read_reg(ks_dev, KS_REMAIN);
}

static int ks_dev_open(struct inode *iptr, struct file *fptr)
{
	struct nuc990_ks_dev *ks_dev;
	unsigned long timeout;

	ks_dev = container_of(fptr->private_data, struct nuc990_ks_dev, miscdev);

	/* Start Key Store Initial */
	nuc990_write_reg(ks_dev, KS_CTL, KS_CTL_INIT | KS_CTL_START);

	/* Waiting for KeyStore initilization done */
	timeout = jiffies + msecs_to_jiffies(KS_BUSY_TIMEOUT);
	while ((nuc990_read_reg(ks_dev, KS_STS) & KS_STS_INITDONE) == 0) {
		if (time_after(jiffies, timeout))
			return -EIO;
		cpu_relax();
	}

	/* Waiting for processing */
	if (nuc990_ks_wait_busy_clear(ks_dev) != 0)
		return -EBUSY;

	return 0;
}

static int ks_dev_release(struct inode *iptr, struct file *fptr)
{
	return 0;
}

static long ks_dev_ioctl(struct file *fptr, unsigned int cmd, unsigned long data)
{
	struct nuc990_ks_dev *ks_dev;
	char __user *argp = (char __user *)data;
	int rval = -EINVAL;

	ks_dev = container_of(fptr->private_data, struct nuc990_ks_dev, miscdev);

	if (_IOC_TYPE(cmd) != NUC990_KS_MAGIC)
		return -ENOTTY;

	switch (cmd) {
	case NU_KS_IOCTL_READ_SRAM:
		rval = nuc990_ks_read(ks_dev, KS_SRAM, argp);
		break;

	case NU_KS_IOCTL_WRITE_SRAM:
		rval = nuc990_ks_write(ks_dev, KS_SRAM, argp);
		break;

	case NU_KS_IOCTL_ERASE_SRAM:
		rval = nuc990_ks_erase(ks_dev, KS_SRAM, argp);
		break;

	case NU_KS_IOCTL_ERASE_ALL:
		rval = nuc990_ks_erase_all(ks_dev);
		break;

	case NU_KS_IOCTL_GET_REMAIN:
		rval = nuc990_ks_remain(ks_dev);
		break;

	case NU_KS_IOCTL_READ_OTP:
		rval = nuc990_ks_read(ks_dev, KS_OTP, argp);
		break;

	case NU_KS_IOCTL_WRITE_OTP:
		rval = nuc990_ks_write(ks_dev, KS_OTP, argp);
		break;

	case NU_KS_IOCTL_ERASE_OTP:
		rval = nuc990_ks_erase(ks_dev, KS_OTP, argp);
		break;

	default:
		dev_warn(ks_dev->dev, "Unknown ioctl cmd: %u\n", cmd);
		break;
	}
	return rval;
}

static const struct file_operations nuc990_ks_fops = {
	.owner = THIS_MODULE,
	.open = ks_dev_open,
	.release = ks_dev_release,
	.unlocked_ioctl = ks_dev_ioctl,
	.compat_ioctl = ks_dev_ioctl,
};

static int nuc990_ks_probe(struct platform_device *pdev)
{
	struct device *dev = &pdev->dev;
	struct nuc990_ks_dev *ks_dev;
	struct resource *res;
	int ret;

	ks_dev = devm_kzalloc(&pdev->dev, sizeof(*ks_dev), GFP_KERNEL);
	if (!ks_dev)
		return -ENOMEM;

	ks_dev->dev = dev;
	res = platform_get_resource(pdev, IORESOURCE_MEM, 0);
	ks_dev->reg_base = devm_ioremap_resource(dev, res);
	if (IS_ERR(ks_dev->reg_base))
		return PTR_ERR(ks_dev->reg_base);

	ks_dev->clk = devm_clk_get(dev, "ks_gate");
	if (IS_ERR(ks_dev->clk))
		return PTR_ERR(ks_dev->clk);

	ret = clk_prepare_enable(ks_dev->clk);
	if (ret)
		return dev_err_probe(dev, ret, "Failed to enable ks_gate clk\n");

		/* Save driver private data */
	platform_set_drvdata(pdev, ks_dev);

	ks_dev->miscdev.minor = MISC_DYNAMIC_MINOR;
	ks_dev->miscdev.name = MISCDEV_NAME;
	ks_dev->miscdev.fops = &nuc990_ks_fops;
	ks_dev->miscdev.parent = dev;
	ret = misc_register(&ks_dev->miscdev);
	if (ret) {
		clk_disable_unprepare(ks_dev->clk);
		dev_err(dev, "error:%d. Unable to register device", ret);
		return ret;
	}

	dev_info(dev, "NUC990 Key Store initialized\n");

	return 0;
}

static int nuc990_ks_remove(struct platform_device *pdev)
{
	struct nuc990_ks_dev *ks_dev;

	ks_dev = platform_get_drvdata(pdev);

	clk_disable_unprepare(ks_dev->clk);

	misc_deregister(&ks_dev->miscdev);

	return 0;
}

static const struct of_device_id nuc990_ks_of_match[] = {
	{ .compatible = "nuvoton,nuc990-keystore" },
	{ /* end of table */ }
};
MODULE_DEVICE_TABLE(of, nuc990_ks_of_match);

static struct platform_driver nuc990_ks_driver = {
	.probe = nuc990_ks_probe,
	.remove = nuc990_ks_remove,
	.driver = {
		.name = "nuc990-keystore",
		.of_match_table = nuc990_ks_of_match,
	},
};

module_platform_driver(nuc990_ks_driver);

MODULE_LICENSE("GPL v2");
MODULE_AUTHOR("Nuvoton, Inc");
MODULE_DESCRIPTION("Nuvoton NUC990 Key Store Driver");
