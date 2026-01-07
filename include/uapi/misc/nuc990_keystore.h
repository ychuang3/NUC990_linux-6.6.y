/* SPDX-License-Identifier: GPL-2.0+ WITH Linux-syscall-note */
/*
 * Nuvoton NUC990 Key Store
 *
 * Copyright (c) 2025 Nuvoton technology corporation.
 */
#ifndef __NUC990_KEYSTORE_H__
#define __NUC990_KEYSTORE_H__

#include <linux/bits.h>
#include <linux/types.h>

#define KS_CTL			0x00
#define KS_METADATA		0x04
#define KS_STS			0x08
#define KS_REMAIN		0x0C
#define KS_KEY(x)		(0x20 + ((x) * 0x04))
#define KS_KEY_REG_CNT		(8)
#define KS_OTPSTS		0x40
#define KS_REMKCNT		0x44

/* KS_CTL register bit fields */
#define KS_CTL_START		BIT(0)
#define KS_CTL_OPMODE_Pos	1
#define KS_CTL_OPMODE_Msk	GENMASK(3, 1)
#define KS_OP_READ		(0x0 << KS_CTL_OPMODE_Pos)
#define KS_OP_WRITE		(0x1 << KS_CTL_OPMODE_Pos)
#define KS_OP_ERASE		(0x2 << KS_CTL_OPMODE_Pos)
#define KS_OP_ERASE_ALL		(0x3 << KS_CTL_OPMODE_Pos)
#define KS_OP_REVOKE		(0x4 << KS_CTL_OPMODE_Pos)
#define KS_OP_REMAN		(0x5 << KS_CTL_OPMODE_Pos)
#define KS_CTL_CONT		BIT(7)
#define KS_CTL_INIT		BIT(8)
#define KS_CTL_IEN		BIT(15)

/* KS_STS register bit fields */
#define KS_STS_IF		BIT(0)
#define KS_STS_EIF		BIT(1)
#define KS_STS_BUSY		BIT(2)
#define KS_STS_SRAMFULL		BIT(3)
#define KS_STS_INITDONE		BIT(7)

#define KS_METADATA_READ_Msk	BIT(2)
#define KS_METADATA_SIZE_Pos	(8)
#define KS_METADATA_SIZE_Msk	GENMASK(12, 8)
#define KS_METADATA_OWNER_Pos	(16)
#define KS_METADATA_OWNER_Msk	GENMASK(18, 16)
#define KS_METADATA_NUMBER_Pos	(20)
#define KS_METADATA_NUMBER_Msk	GENMASK(25, 20)
#define KS_METADATA_DST_Pos	(30)
#define KS_METADATA_DST_Msk	GENMASK(31, 30)

#define KS_OWNER_AES		(0ul)
#define KS_OWNER_HMAC		(1ul)
#define KS_OWNER_ECC		(4ul)
#define KS_OWNER_CPU		(5ul)

#define KS_META_AES		(0ul << KS_METADATA_OWNER_Pos)
#define KS_META_HMAC		(1ul << KS_METADATA_OWNER_Pos)
#define KS_META_ECC		(4ul << KS_METADATA_OWNER_Pos)
#define KS_META_CPU		(5ul << KS_METADATA_OWNER_Pos)

#define KS_META_128		(0ul << KS_METADATA_SIZE_Pos)
#define KS_META_163		(1ul << KS_METADATA_SIZE_Pos)
#define KS_META_192		(2ul << KS_METADATA_SIZE_Pos)
#define KS_META_224		(3ul << KS_METADATA_SIZE_Pos)
#define KS_META_233		(4ul << KS_METADATA_SIZE_Pos)
#define KS_META_255		(5ul << KS_METADATA_SIZE_Pos)
#define KS_META_256		(6ul << KS_METADATA_SIZE_Pos)
#define KS_META_283		(7ul << KS_METADATA_SIZE_Pos)
#define KS_META_384		(8ul << KS_METADATA_SIZE_Pos)
#define KS_META_409		(9ul << KS_METADATA_SIZE_Pos)
#define KS_META_512		(10ul << KS_METADATA_SIZE_Pos)
#define KS_META_521		(11ul << KS_METADATA_SIZE_Pos)
#define KS_META_571		(12ul << KS_METADATA_SIZE_Pos)
#define KS_META_1024		(16ul << KS_METADATA_SIZE_Pos)
#define KS_META_1536		(17ul << KS_METADATA_SIZE_Pos)

#define KS_TOMETAKEY(x)		(((x) << KS_METADATA_NUMBER_Pos) & KS_METADATA_NUMBER_Msk)
#define KS_TOKEYIDX(x)		(((x) & KS_METADATA_NUMBER_Msk) >> KS_METADATA_NUMBER_Pos)

enum ks_key_type {
	KS_SRAM = 0x0,
	KS_OTP  = 0x2,
};

#define KS_SRAM_KEY_CNT		(32)
#define KS_OTP_KEY_CNT		(9)

#define NUC990_KS_MAGIC		'K'

#define NU_KS_IOCTL_READ_SRAM	_IOWR(NUC990_KS_MAGIC, 1, struct ks_read_args)
#define NU_KS_IOCTL_WRITE_SRAM	_IOWR(NUC990_KS_MAGIC, 2, struct ks_write_args)
#define NU_KS_IOCTL_ERASE_SRAM	_IOWR(NUC990_KS_MAGIC, 4, unsigned long)
#define NU_KS_IOCTL_ERASE_ALL	_IOWR(NUC990_KS_MAGIC, 5, unsigned long)
#define NU_KS_IOCTL_GET_REMAIN	_IOR(NUC990_KS_MAGIC, 7, unsigned long)

#define NU_KS_IOCTL_READ_OTP	_IOWR(NUC990_KS_MAGIC, 11, struct ks_read_args)
#define NU_KS_IOCTL_WRITE_OTP	_IOWR(NUC990_KS_MAGIC, 12, struct ks_write_args)
#define NU_KS_IOCTL_ERASE_OTP	_IOWR(NUC990_KS_MAGIC, 14, unsigned long)

struct ks_read_args {
	__u32 type;
	__u32 key_idx;
	__u32 word_cnt;
	__u32 key[48];
};

struct ks_write_args {
	__u32 type;
	__u32 key_idx;
	__u32 meta_data;
	__u32 word_cnt;
	__u32 key[48];
};

#endif /* __NUC990_KEYSTORE_H__ */
