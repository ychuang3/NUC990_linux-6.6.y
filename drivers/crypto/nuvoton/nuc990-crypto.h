/* SPDX-License-Identifier: GPL-2.0+ WITH Linux-syscall-note */
/*
 * Nuvoton Cryptographic Accelerator registers and data
 *
 * Copyright (c) 2026 Nuvoton technology corporation.
 *
 * This	program	is free	software; you can redistribute it and/or modify
 * it under the	terms of the GNU General Public	License	as published by
 * the Free Software Foundation;version	2 of the License.
 *
 */
#ifndef	__NUVOTON_CRYPTO_H__
#define	__NUVOTON_CRYPTO_H__

#include <linux/bitops.h>
#include <linux/interrupt.h>
#include <linux/list.h>
#include <linux/mm.h>
#include <linux/spinlock.h>
#include <linux/scatterlist.h>
#include <crypto/scatterwalk.h>
#include <linux/types.h>

#define	INTEN			0x000
#define	INTEN_AESIEN			BIT(0)
#define	INTEN_AESEIEN			BIT(1)
#define	INTEN_PRNGIEN			BIT(16)
#define	INTEN_PRNGEIEN			BIT(17)
#define	INTEN_ECCIEN			BIT(22)
#define	INTEN_ECCEIEN			BIT(23)
#define	INTEN_HMACIEN			BIT(24)
#define	INTEN_HMACEIEN			BIT(25)
#define	INTEN_RSAIEN			BIT(30)
#define	INTEN_RSAEIEN			BIT(31)
#define	INTSTS			0x004
#define	INTSTS_AESIF			BIT(0)
#define	INTSTS_AESEIF			BIT(1)
#define	INTSTS_PRNGIF			BIT(16)
#define	INTSTS_PRNGEIF			BIT(17)
#define	INTSTS_ECCIF			BIT(22)
#define	INTSTS_ECCEIF			BIT(23)
#define	INTSTS_HMACIF			BIT(24)
#define	INTSTS_HMACEIF			BIT(25)
#define	INTSTS_RSAIF			BIT(30)
#define	INTSTS_RSAEIF			BIT(31)

#define	PRNG_CTL		0x008
#define	PRNG_CTL_START			BIT(0)
#define	PRNG_CTL_SEEDRLD		BIT(1)
#define	PRNG_CTL_KEYSZ_OFFSET		(2)
#define	PRNG_CTL_KEYSZ_MASK		GENMASK(5, 2)
#define	PRNG_CTL_BUSY			BIT(8)
#define	PRNG_SEED		0x00C
#define	PRNG_KEY(x)		(0x010 + ((x) *	0x04))

#define	AES_FDBCK(x)		(0x050 + ((x) *	0x04))
#define	AES_GCM_IVCNT(x)	(0x080 + ((x) *	0x04))
#define	AES_GCM_ACNT(x)		(0x088 + ((x) *	0x04))
#define	AES_GCM_PCNT(x)		(0x090 + ((x) *	0x04))
#define	AES_FBADDR		0x0A0
#define	AES_CTL			0x100
#define	AES_CTL_START			BIT(0)
#define	AES_CTL_STOP			BIT(1)
#define	AES_CTL_KEYSZ_OFFSET		2
#define	AES_CTL_KEYSZ_MASK		GENMASK(3, 2)
#define	AES_CTL_DMALAST			BIT(5)
#define	AES_CTL_DMACSCAD		BIT(6)
#define	AES_CTL_DMAEN			BIT(7)
#define	AES_CTL_OPMODE_OFFSET		8
#define	AES_CTL_OPMODE_MASK		GENMASK(15, 8)
#define	AES_CTL_ENCRYPT			BIT(16)
#define	AES_CTL_FBIN			BIT(20)
#define	AES_CTL_FBOUT			BIT(21)
#define	AES_CTL_OUTSWAP			BIT(22)
#define	AES_CTL_INSWAP			BIT(23)
#define	AES_CTL_KOUTSWAP		BIT(24)
#define	AES_CTL_KINSWAP			BIT(25)
#define	AES_STS			0x104
#define	AES_STS_BUSY			BIT(0)
#define	AES_STS_INBUFEMPTY		BIT(8)
#define	AES_STS_INBUFFULL		BIT(9)
#define	AES_STS_INBUFERR		BIT(10)
#define	AES_STS_CNTERR			BIT(12)
#define	AES_STS_OUTBUFEMPTY		BIT(16)
#define	AES_STS_OUTBUFFULL		BIT(17)
#define	AES_STS_OUTBUFERR		BIT(18)
#define	AES_STS_BUSERR			BIT(20)
#define	AES_STS_KSERR			BIT(21)
#define	AES_DATIN		0x108
#define	AES_DATOUT		0x10C
#define	AES_KEY(x)		(0x110 + ((x) *	0x04))
#define	AES_IV(x)		(0x130 + ((x) *	0x04))
#define	AES_SADDR		0x140
#define	AES_DADDR		0x144
#define	AES_CNT			0x148

#define	HMAC_CTL		0x300
#define	HMAC_CTL_START			BIT(0)
#define	HMAC_CTL_STOP			BIT(1)
#define	HMAC_CTL_DMAFIRST		BIT(4)
#define	HMAC_CTL_DMALAST		BIT(5)
#define	HMAC_CTL_DMACSCAD		BIT(6)
#define	HMAC_CTL_DMAEN			BIT(7)
#define	HMAC_CTL_OPMODE_OFFSET		8
#define	HMAC_CTL_OPMODE_MASK		GENMASK(10, 8)
#define	HMAC_CTL_HMACEN			BIT(11)
#define	HMAC_CTL_SHA3EN			BIT(12)
#define	HMAC_CTL_MD5EN			BIT(14)
#define	HMAC_CTL_FBIN			BIT(20)
#define	HMAC_CTL_FBOUT			BIT(21)
#define	HMAC_CTL_OUTSWAP		BIT(22)
#define	HMAC_CTL_INSWAP			BIT(23)
#define	HMAC_CTL_NEXTDGST		BIT(24)
#define	HMAC_CTL_FINISHDGST		BIT(25)
#define	HMAC_STS		0x304
#define	HMAC_STS_BUSY			BIT(0)
#define	HMAC_STS_DMABUSY		BIT(1)
#define	HMAC_STS_SHAKEBUSY		BIT(2)
#define	HMAC_STS_DMAERR			BIT(8)
#define	HMAC_STS_KSERR			BIT(9)
#define	HMAC_STS_DATINREQ		BIT(16)
#define	HMAC_DGST(x)		(0x308 + ((x) *	0x04))
#define	HMAC_KEYCNT		0x348
#define	HMAC_SADDR		0x34C
#define	HMAC_DMACNT		0x350
#define	HMAC_DATIN		0x354
#define	HMAC_FBADDR		0x4FC
#define	HMAC_SHAKEDGST(x)	(0x500 + ((x) *	0x04))
#define	HMAC_SHAKEDGST_WCNT		42

#define	ECC_CTL			0x800
#define	ECC_CTL_START			BIT(0)
#define	ECC_CTL_STOP			BIT(1)
#define	ECC_CTL_ECDSAS			BIT(4)
#define	ECC_CTL_ECDSAR			BIT(5)
#define	ECC_CTL_DMAEN			BIT(7)
#define	ECC_CTL_FSEL			BIT(8)
#define	ECC_CTL_ECCOP_OFFSET		9
#define	ECC_CTL_ECCOP_MASK		GENMASK(10, 9)
#define	ECC_CTL_MODOP_OFFSET		11
#define	ECC_CTL_MODOP_MASK		GENMASK(12, 11)
#define	ECC_CTL_SPCEN			BIT(13)
#define	ECC_CTL_SPCSEL			BIT(16)
#define	ECC_CTL_CURVEM_OFFSET		22
#define	ECC_CTL_CURVEM_MASK		GENMASK(31, 22)
#define	ECC_STS			0x804
#define	ECC_STS_BUSY			BIT(0)
#define	ECC_STS_DMABUSY			BIT(1)
#define	ECC_STS_BUSERR			BIT(16)
#define	ECC_STS_KSERR			BIT(17)
#define	ECC_STS_ECDSAERR		BIT(19)
#define	ECC_X1			0x808
#define	ECC_Y1			0x850
#define	ECC_X2			0x898
#define	ECC_Y2			0x8E0
#define	ECC_A			0x928
#define	ECC_B			0x970
#define	ECC_N			0x9B8
#define	ECC_K			0xA00
#define	ECC_KEY_WCNT			18
#define	ECC_SADDR		0xA48
#define	ECC_DADDR		0xA4C
#define	ECC_STARTREG		0xA50
#define	ECC_WORDCNT		0xA54
#define	ECC_DMA_CTL		0xA58
#define	ECC_DMA_CTL_LDP1		BIT(0)
#define	ECC_DMA_CTL_LDP2		BIT(1)
#define	ECC_DMA_CTL_LDA			BIT(2)
#define	ECC_DMA_CTL_LDB			BIT(3)
#define	ECC_DMA_CTL_LDN			BIT(4)
#define	ECC_DMA_CTL_LDK			BIT(5)
#define	ECC_PRNGSEED		0xA60
#define	ECC_PRNGSTS		0xA64
#define	ECC_PRNGSTS_RDCNT		BIT(8)
#define	ECC_EDDSA		0xA70
#define	ECC_EDDSA_SHASP			BIT(0)
#define	ECC_EDDSA_ECCSB			BIT(1)
#define	ECC_EDDSA_SHALP			BIT(2)
#define	ECC_EDDSA_ECCSR			BIT(3)
#define	ECC_EDDSA_ECCRB			BIT(4)
#define	ECC_EDDSA_ECCSG			BIT(5)

#define	RSA_CTL			0xB00
#define	RSA_CTL_START			BIT(0)
#define	RSA_CTL_STOP			BIT(1)
#define	RSA_CTL_CRT			BIT(2)
#define	RSA_CTL_CRTBYP			BIT(3)
#define	RSA_CTL_KEYLENG_OFFSET		4
#define	RSA_CTL_KEYLENG_MASK		GENMASK(5, 4)
#define	RSA_STS			0xB04
#define	RSA_STS_BUSY			BIT(0)
#define	RSA_STS_DMABUSY			BIT(1)
#define	RSA_STS_BUSERR			BIT(16)
#define	RSA_STS_CTLERR			BIT(17)
#define	RSA_SADDR0		0xB08
#define	RSA_SADDR1		0xB0C
#define	RSA_SADDR2		0xB10
#define	RSA_SADDR3		0xB14
#define	RSA_SADDR4		0xB18
#define	RSA_DADDR		0xB1C
#define	RSA_MADDR0		0xB20
#define	RSA_MADDR1		0xB24
#define	RSA_MADDR2		0xB28
#define	RSA_MADDR3		0xB2C
#define	RSA_MADDR4		0xB30
#define	RSA_MADDR5		0xB34
#define	RSA_MADDR6		0xB38

#define	PRNG_KSCTL		0xF00
#define	PRNG_KSCTL_NUM_OFFSET		0
#define	PRNG_KSCTL_NUM_MASK		GENMASK(4, 0)
#define	PRNG_KSCTL_WDST			BIT(21)
#define	PRNG_KSCTL_WSDST_OFFSET		22
#define	PRNG_KSCTL_WSDST_MASK		GENMASK(23, 22)
#define	PRNG_KSCTL_OWNER_OFFSET		24
#define	PRNG_KSCTL_OWNER_MASK		GENMASK(26, 24)

#define	AES_KSCTL		0xF10
#define	AES_KSCTL_NUM_OFFSET		0
#define	AES_KSCTL_NUM_MASK		GENMASK(4, 0)
#define	AES_KSCTL_RSRC			BIT(5)
#define	AES_KSCTL_RSSRC_OFFSET		6
#define	AES_KSCTL_RSSRC_MASK		GENMASK(7, 6)

#define	HMAC_KSCTL		0xF30
#define	HMAC_KSCTL_NUM_OFFSET		0
#define	HMAC_KSCTL_NUM_MASK		GENMASK(4, 0)
#define	HMAC_KSCTL_RSRC			BIT(5)
#define	HMAC_KSCTL_RSSRC_OFFSET		6
#define	HMAC_KSCTL_RSSRC_MASK		GENMASK(7, 6)

#define	ECC_KSCTL		0xF40
#define	ECC_KSCTL_NUMK_OFFSET		0
#define	ECC_KSCTL_NUMK_MASK		GENMASK(4, 0)
#define	ECC_KSCTL_RSRCK			BIT(5)
#define	ECC_KSCTL_RSSRCK_OFFSET		6
#define	ECC_KSCTL_RSSRCK_MASK		GENMASK(7, 6)
#define	ECC_KSCTL_XY			BIT(20)
#define	ECC_KSCTL_WDST			BIT(21)
#define	ECC_KSCTL_WSDST_OFFSET		22
#define	ECC_KSCTL_WSDST_MASK		GENMASK(23, 22)
#define	ECC_KSCTL_OWNER_OFFSET		24
#define	ECC_KSCTL_OWNER_MASK		GENMASK(26, 24)
#define	ECC_KSSTS		0xF44
#define	ECC_KSSTS_NUM_OFFSET		0
#define	ECC_KSSTS_NUM_MASK		(0x1f << 0)
#define	ECC_KSXY			0xF48
#define	ECC_KSXY_NUMX1_OFFSET		0
#define	ECC_KSXY_NUMX1_MASK		GENMASK(4, 0)
#define	ECC_KSXY_RSRCXY1		BIT(5)
#define	ECC_KSXY_RSSRCX1_OFFSET		6
#define	ECC_KSXY_RSSRCX1_MASK		GENMASK(7, 6)
#define	ECC_KSXY_NUMY1_OFFSET		8
#define	ECC_KSXY_NUMY1_MASK		GENMASK(12, 8)
#define	ECC_KSXY_RSSRCY1_OFFSET		14
#define	ECC_KSXY_RSSRCY1_MASK		GENMASK(15, 14)

#define	AES_KEYSZ_SEL_128	(0x0 <<	AES_CTL_KEYSZ_OFFSET)
#define	AES_KEYSZ_SEL_192	(0x1 <<	AES_CTL_KEYSZ_OFFSET)
#define	AES_KEYSZ_SEL_256	(0x2 <<	AES_CTL_KEYSZ_OFFSET)

#define	AES_MODE_ECB		(0x00 << AES_CTL_OPMODE_OFFSET)
#define	AES_MODE_CBC		(0x01 << AES_CTL_OPMODE_OFFSET)
#define	AES_MODE_CFB		(0x02 << AES_CTL_OPMODE_OFFSET)
#define	AES_MODE_OFB		(0x03 << AES_CTL_OPMODE_OFFSET)
#define	AES_MODE_CTR		(0x04 << AES_CTL_OPMODE_OFFSET)
#define	AES_MODE_CBC_CS1	(0x10 << AES_CTL_OPMODE_OFFSET)
#define	AES_MODE_CBC_CS2	(0x11 << AES_CTL_OPMODE_OFFSET)
#define	AES_MODE_CBC_CS3	(0x12 << AES_CTL_OPMODE_OFFSET)
#define	AES_MODE_GCM		(0x20 << AES_CTL_OPMODE_OFFSET)
#define	AES_MODE_GHASH		(0x21 << AES_CTL_OPMODE_OFFSET)
#define	AES_MODE_CCM		(0x22 << AES_CTL_OPMODE_OFFSET)

#define	SHA_OPMODE_SHA1		(0x0 <<	HMAC_CTL_OPMODE_OFFSET)
#define	SHA_OPMODE_SHA224	(0x5 <<	HMAC_CTL_OPMODE_OFFSET)
#define	SHA_OPMODE_SHA256	(0x4 <<	HMAC_CTL_OPMODE_OFFSET)
#define	SHA_OPMODE_SHA384	(0x7 <<	HMAC_CTL_OPMODE_OFFSET)
#define	SHA_OPMODE_SHA512	(0x6 <<	HMAC_CTL_OPMODE_OFFSET)
#define	SHA_OPMODE_SHAKE128	(0x0 <<	HMAC_CTL_OPMODE_OFFSET)
#define	SHA_OPMODE_SHAKE256	(0x1 <<	HMAC_CTL_OPMODE_OFFSET)

#define	ECCOP_POINT_MUL		(0x0 <<	ECC_CTL_ECCOP_OFFSET)
#define	ECCOP_MODULE		(0x1 <<	ECC_CTL_ECCOP_OFFSET)
#define	ECCOP_POINT_ADD		(0x2 <<	ECC_CTL_ECCOP_OFFSET)
#define	ECCOP_POINT_DOUBLE	(0x3 <<	ECC_CTL_ECCOP_OFFSET)

#define	MODOP_DIV		(0x0 <<	ECC_CTL_MODOP_OFFSET)
#define	MODOP_MUL		(0x1 <<	ECC_CTL_MODOP_OFFSET)
#define	MODOP_ADD		(0x2 <<	ECC_CTL_MODOP_OFFSET)
#define	MODOP_SUB		(0x3 <<	ECC_CTL_MODOP_OFFSET)

#define	AES_BUFF_SIZE		(PAGE_SIZE)
#define	SHA_BUFF_SIZE		(PAGE_SIZE * 4)
#define	SHA_FDBCK_SIZE		(PAGE_SIZE)
#define	HMAC_KEY_BUFF_SIZE	(1024)
#define	AES_KS_KEYLEN		17

struct nu_crypto_dev;

/*-------------------------------------------------------------------------*/
/*   AES								   */
/*-------------------------------------------------------------------------*/

struct nu_aes_dev;

typedef	int (*nu_aes_fn_t)(struct nu_aes_dev *,	int);

struct nu_aes_base_ctx {
	struct nu_aes_dev *dd;
	nu_aes_fn_t     start;
	u32             mode;
	int             keylen;
	u32             keysz_sel;
	u32             aes_key[8];
	u8              tag[16];
	int             authsize;
	int             assoclen;
	int             text_len;
};

struct nu_aes_ctx {
	struct nu_aes_base_ctx  base;
};

struct nu_aes_dev {
	struct list_head        list;
	struct device           *dev;
	struct nu_crypto_dev    *nu_cdev;
	void __iomem            *reg_base;
	u32                     flags;
	struct crypto_async_request *areq;
	struct nu_aes_base_ctx  *ctx;
	nu_aes_fn_t             resume;
	spinlock_t              lock;
	struct crypto_queue     queue;
	struct tasklet_struct   done_task;
	struct tasklet_struct   queue_task;
	u8                      inbuf[AES_BUFF_SIZE] __aligned(32);
	dma_addr_t              dma_inbuf;
	u8                      outbuf[AES_BUFF_SIZE] __aligned(32);
	dma_addr_t              dma_outbuf;
	int                     req_len;
	int                     dma_len;
	struct scatterlist      *in_sg;
	struct scatterlist      *out_sg;
	int                     in_sg_off;
	int                     out_sg_off;
};

/*-------------------------------------------------------------------------*/
/*   SHA								   */
/*-------------------------------------------------------------------------*/

struct nu_sha_dev;

struct nu_sha_ctx {
	struct nu_sha_dev *dd;
	u32             hash_mode;
	int             hmac_key_len; /* HMAC key length in bytes  */
	int             keybufcnt;
	u8              keybuf[HMAC_KEY_BUFF_SIZE] __aligned(32);
};

struct nu_sha_reqctx {
	struct nu_sha_dev *dd;
	u32             flags;
	u32             op;
	u32             reg_ctl;
	int             digest_len;
	int             block_size;
	int             dma_max_size;
	struct scatterlist *sg;
	u32             sg_off;
	u32             req_len;
	int             bufcnt;
	u8              *buffer;
	dma_addr_t      dma_buff;
	u8              fdbck[SHA_FDBCK_SIZE] __aligned(32);
	dma_addr_t      dma_fdbck;
};

struct nu_sha_dev {
	struct list_head        list;
	struct device           *dev;
	struct nu_crypto_dev    *nu_cdev;
	void __iomem            *reg_base;
	u32                     flags;
	spinlock_t              lock;
	struct crypto_queue     queue;
	struct ahash_request    *req;
	struct tasklet_struct   done_task;
	struct tasklet_struct   queue_task;
};

/*-------------------------------------------------------------------------*/
/*   ECC								   */
/*-------------------------------------------------------------------------*/

#define	NU_ECC_MAX_LEN		(76)	/* 571/8 + 1 + 4 */

enum {
	CURVE_P_192  = 0x01,
	CURVE_P_224  = 0x02,
	CURVE_P_256  = 0x03,
	CURVE_P_384  = 0x04,
	CURVE_P_521  = 0x05,
	CURVE_K_163  = 0x11,
	CURVE_K_233  = 0x12,
	CURVE_K_283  = 0x13,
	CURVE_K_409  = 0x14,
	CURVE_K_571  = 0x15,
	CURVE_B_163  = 0x21,
	CURVE_B_233  = 0x22,
	CURVE_B_283  = 0x23,
	CURVE_B_409  = 0x24,
	CURVE_B_571  = 0x25,
	CURVE_KO_192 = 0x31,
	CURVE_KO_224 = 0x32,
	CURVE_KO_256 = 0x33,
	CURVE_BP_256 = 0x41,
	CURVE_BP_384 = 0x42,
	CURVE_BP_512 = 0x43,
	CURVE_25519  = 0x51,
	CURVE_UNDEF,
};

enum {
	CURVE_GF_P,
	CURVE_GF_2M,
};

struct nu_ecc_curve {
	int	curve_id;
	int	Echar;
	u8	Ea[NU_ECC_MAX_LEN];
	u8	Eb[NU_ECC_MAX_LEN];
	u8	Px[NU_ECC_MAX_LEN];
	u8	Py[NU_ECC_MAX_LEN];
	int	Epl;
	u8	Pp[NU_ECC_MAX_LEN];
	int	Eol;
	u8	Eorder[72];
	int	key_len;
	int	irreducible_k1;
	int	irreducible_k2;
	int	irreducible_k3;
	int	GF;
};

struct nu_ecc_ctx {
	struct nu_ecc_dev *dd;
	int	curve_id;
	int	ndigits;
	const struct nu_ecc_curve *curve;
	int	keylen;
	u8	private_key[NU_ECC_MAX_LEN];
};

struct nu_ecc_dev {
	struct list_head	list;
	struct device		*dev;
	struct nu_crypto_dev	*nu_cdev;
	void __iomem		*reg_base;
	spinlock_t		lock;
};

/*-------------------------------------------------------------------------*/
/*   RSA								   */
/*-------------------------------------------------------------------------*/

#define	NU_RSA_MAX_BIT_LEN	(4096)
#define	NU_RSA_MAX_BYTE_LEN	(NU_RSA_MAX_BIT_LEN/8)
#define	NU_RSA_MAX_WORD_LEN	(NU_RSA_MAX_BIT_LEN/32)

#define	RSA_REG_RAM_SIZE	(NU_RSA_MAX_BYTE_LEN)
#define	RSA_BUFF_SIZE		(RSA_REG_RAM_SIZE * 13)

#define	M_OFF			(0)
#define	N_OFF			(RSA_REG_RAM_SIZE)
#define	E_OFF			(RSA_REG_RAM_SIZE * 2)
#define	D_OFF			(RSA_REG_RAM_SIZE * 2)
#define	P_OFF			(RSA_REG_RAM_SIZE * 3)
#define	Q_OFF			(RSA_REG_RAM_SIZE * 4)
#define	MADR0_OFF		(RSA_REG_RAM_SIZE * 5)
#define	MADR1_OFF		(RSA_REG_RAM_SIZE * 6)
#define	MADR2_OFF		(RSA_REG_RAM_SIZE * 7)
#define	MADR3_OFF		(RSA_REG_RAM_SIZE * 8)
#define	MADR4_OFF		(RSA_REG_RAM_SIZE * 9)
#define	MADR5_OFF		(RSA_REG_RAM_SIZE * 10)
#define	MADR6_OFF		(RSA_REG_RAM_SIZE * 11)
#define	ANS_OFF			(RSA_REG_RAM_SIZE * 12)

struct nu_rsa_ctx {
	struct nu_rsa_dev	*dd;
	void __iomem		*reg_base;
	int			rsa_bit_len;
	u8			buffer[RSA_BUFF_SIZE] __aligned(32);
	u8			public_key[RSA_REG_RAM_SIZE];
	int			public_key_size;
	u8			private_key[RSA_REG_RAM_SIZE];
	int			private_key_size;
	dma_addr_t		dma_buff;
};

struct nu_rsa_dev {
	struct list_head	list;
	struct device		*dev;
	struct nu_crypto_dev	*nu_cdev;
	void __iomem		*reg_base;
};

struct nu_crypto_dev {
	struct device		*dev;
	void __iomem		*reg_base;
	unsigned long		prng;
	struct nu_aes_dev	aes_dd;
	struct nu_sha_dev	sha_dd;
	struct nu_ecc_dev	ecc_dd;
	struct nu_rsa_dev	rsa_dd;
	bool			ecc_ioctl;
	bool			rsa_ioctl;
};

/*-------------------------------------------------------------------------*/
/*   ECC and RSA IOCTL commands						   */
/*-------------------------------------------------------------------------*/
#define	CRYPTO_IOC_MAGIC	'C'
#define	RSA_IOC_SET_BITLEN	_IOW(CRYPTO_IOC_MAGIC, 20, unsigned long)
#define	RSA_IOC_SET_N		_IOW(CRYPTO_IOC_MAGIC, 21, u8	*)
#define	RSA_IOC_SET_E		_IOW(CRYPTO_IOC_MAGIC, 22, u8	*)
#define	RSA_IOC_SET_M		_IOW(CRYPTO_IOC_MAGIC, 23, u8	*)
#define	RSA_IOC_SET_P		_IOW(CRYPTO_IOC_MAGIC, 24, u8	*)
#define	RSA_IOC_SET_Q		_IOW(CRYPTO_IOC_MAGIC, 25, u8	*)
#define	RSA_IOC_RUN		_IOW(CRYPTO_IOC_MAGIC, 29, u8	*)

#define	ECC_IOC_KEY_GEN		_IOW(CRYPTO_IOC_MAGIC, 53, u8	*)
#define	ECC_IOC_POINT_MUL	_IOW(CRYPTO_IOC_MAGIC, 55, u8	*)
#define	ECC_IOC_SIG_GEN		_IOW(CRYPTO_IOC_MAGIC, 57, u8	*)
#define	ECC_IOC_SIG_VERIFY	_IOW(CRYPTO_IOC_MAGIC, 58, u8	*)

#define	ECC_KMAXL		160

struct ecc_args_t {
	int curve;
	int keylen;		/* Key Store key is OTP	key if bit7 is 1, otherwise is SRAM key. */
				/* For example,	0x84 is	OTP key	4, while 0x11 is SRAM key 17 */
	int knum_d;		/* Key Store number of private key; -1 means unused */
	int knum_x;		/* Key Store number of public key X; -1	means unused */
	int knum_y;		/* Key Store number of public key Y; -1	means unused */
	u8  d[ECC_KMAXL];	/* private key (not used if select Key Store key) */
	u8  Qx[ECC_KMAXL];	/* public key X	(not used if select Key	Store key) */
	u8  Qy[ECC_KMAXL];	/* public key Y	(not used if select Key	Store key) */
	u8  sha_dgst[ECC_KMAXL]; /* sha digest of the message(or image) to be verified */
	u8  k[ECC_KMAXL];	/* used	for signature generation */
	u8  R[ECC_KMAXL];	/* ECDSA signature R */
	u8  S[ECC_KMAXL];	/* ECDSA signature S */
	u8  out_x[ECC_KMAXL];	/* output X */
	u8  out_y[ECC_KMAXL];	/* output Y */
};

int nuc990_prng_probe(struct device *dev, void __iomem *reg_base, unsigned long *data);
int nuc990_prng_remove(struct device *dev, unsigned long data);
int nuc990_aes_probe(struct device *dev, struct nu_crypto_dev *crypto_dev);
int nuc990_aes_remove(struct device *dev, struct nu_crypto_dev *crypto_dev);
int nuc990_sha_probe(struct device *dev, struct nu_crypto_dev *crypto_dev);
int nuc990_sha_remove(struct device *dev, struct nu_crypto_dev *crypto_dev);
int nuc990_ecc_probe(struct device *dev, struct nu_crypto_dev *crypto_dev);
int nuc990_ecc_remove(struct device *dev, struct nu_crypto_dev *crypto_dev);
int nuc990_rsa_probe(struct device *dev, struct nu_crypto_dev *crypto_dev);
int nuc990_rsa_remove(struct device *dev, struct nu_crypto_dev *crypto_dev);

#endif
