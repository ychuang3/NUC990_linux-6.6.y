/* SPDX-License-Identifier: GPL-2.0 */
/*
 * Nuvoton NUC990 Timer driver
 *
 * Copyright (C) 2025 Nuvoton Technology Corp.
 */

#ifndef __ASM_ARCH_REGS_TIMER_H
#define __ASM_ARCH_REGS_TIMER_H

#define REG_TIMER_CTL          (0x00)    /* Timer Control Register */
#define REG_TIMER_CMP          (0x04)    /* Timer Comparator Register */
#define REG_TIMER_INTSTS       (0x08)    /* Timer Interrupt Status Register */
#define REG_TIMER_CNT          (0x0C)    /* Timer Data Register */
#define REG_TIMER_CAP          (0x10)    /* Timer Capture Data Register */
#define REG_TIMER_EXTCTL       (0x14)    /* Timer External Control Register */
#define REG_TIMER_EINTSTS      (0x18)    /* Timer External Interrupt Status Register */
#define REG_TIMER_TRGCTL       (0x1C)    /* Timer Trigger Control Register */
#define REG_TIMER_ALTCTL       (0x20)    /* Timer Alternative Control Register */
#define REG_TIMER_PWMCTL       (0x40)    /* Timer PWM Control Register */
#define REG_TIMER_PWMCLKSRC    (0x44)    /* Timer PWM Counter Clock Source Register */
#define REG_TIMER_PWMCLKPSC    (0x48)    /* Timer PWM Counter Clock Pre-scale Register */
#define REG_TIMER_PWMCNTCLR    (0x4C)    /* Timer PWM Clear Counter Register */
#define REG_TIMER_PWMPERIOD    (0x50)    /* Timer PWM Period Register */
#define REG_TIMER_PWMCMPDAT    (0x54)    /* Timer PWM Comparator Register */
#define REG_TIMER_PWMDTCTL     (0x58)    /* Timer PWM Dead-time Control Register */
#define REG_TIMER_PWMCNT       (0x5C)    /* Timer PWM Counter Register */
#define REG_TIMER_PWMMSKEN     (0x60)    /* Timer PWM Output Mask Enable Register */
#define REG_TIMER_PWMMSK       (0x64)    /* Timer PWM Output Mask Data Control Register */
#define REG_TIMER_PWMBNF       (0x68)    /* Timer PWM Brake Pin Noise Filter Register */
#define REG_TIMER_PWMFAILBRK   (0x6C)    /* Timer PWM System Fail Brake Control Register */
#define REG_TIMER_PWMBRKCTL    (0x70)    /* Timer PWM Brake Control Register */
#define REG_TIMER_PWMPOLCTL    (0x74)    /* Timer PWM Pin Output Polar Control Register */
#define REG_TIMER_PWMPOEN      (0x78)    /* Timer PWM Pin Output Enable Register */
#define REG_TIMER_PWMSWBRK     (0x7C)    /* Timer PWM Software Trigger Brake Control Register */
#define REG_TIMER_PWMINTEN0    (0x80)    /* Timer PWM Interrupt Enable Register 0 */
#define REG_TIMER_PWMINTEN1    (0x84)    /* Timer PWM Interrupt Enable Register 1 */
#define REG_TIMER_PWMINTSTS0   (0x88)    /* Timer PWM Interrupt Status Register 0 */
#define REG_TIMER_PWMINTSTS1   (0x8C)    /* Timer PWM Interrupt Status Register 1 */
#define REG_TIMER_PWMEADCTS    (0x90)    /* Timer PWM EADC Trigger Source Select Register */
#define REG_TIMER_PWMSCTL      (0x94)    /* Timer PWM Synchronous Control Register */
#define REG_TIMER_PWMSTRG      (0x98)    /* Timer PWM Synchronous Trigger Register */
#define REG_TIMER_PWMSTATUS    (0x9C)    /* Timer PWM Status Register */
#define REG_TIMER_PWMPBUF      (0xA0)    /* Timer PWM Period Buffer Register */
#define REG_TIMER_PWMCMPBUF    (0xA4)    /* Timer PWM Comparator Buffer Register */

#define CTL_PSC_SHIFT        0
#define CTL_PSC_MASK         GENMASK(7, 0)
#define CTL_PSC_SET(x)       ((x) << CTL_PSC_SHIFT)
#define CTL_FUNCSEL          BIT(15)   /* 0: timer, 1: PWM */
#define CTL_INTRGEN          BIT(19)   /* 0: disabled, 1: enabled */
#define CTL_PERIOSEL         BIT(20)   /* 0: disabled, 1: enabled */
#define CTL_TGLPINSEL        BIT(21)   /* 0: TMx, 1: TMx_EXT */
#define CTL_CAPSRC           BIT(22)   /* 0: TMx_EXT pin, 1: external clock */
#define CTL_EXTCNTEN         BIT(24)   /* 0: disabled, 1: enabled */
#define CTL_ACTSTS           BIT(25)   /* 0: inactive, 1: active */
#define CTL_OPMODE_SHIFT     27
#define CTL_OPMODE_MASK      GENMASK(28, 27)
#define CTL_OPMODE_ONESHOT    (0x0 << CTL_OPMODE_SHIFT)   /* one-shot mode */
#define CTL_OPMODE_PERIODIC   (0x1 << CTL_OPMODE_SHIFT)   /* periodic mode */
#define CTL_OPMODE_TOGGLE     (0x2 << CTL_OPMODE_SHIFT)   /* toggle-output mode */
#define CTL_OPMODE_CONTINUOUS (0x3 << CTL_OPMODE_SHIFT)   /* continuous counting mode */
#define CTL_INTEN            BIT(29)   /* 0: disabled, 1: enabled */
#define CTL_CNTEN            BIT(30)   /* 0: stop, 1: start */
#define CTL_ICEDEBUG         BIT(31)   /* 0: ICE debug affects timer, 1: timer runs regardless */

#define EXTCTL_CNTPHASE        BIT(0)   /* 0: falling edge counted, 1: rising edge counted */
#define EXTCTL_CAPEN           BIT(3)   /* 0: capture disabled, 1: capture enabled */
#define EXTCTL_CAPFUNCS        BIT(4)   /* 0: external capture mode, 1: external reset mode */
#define EXTCTL_CAPIEN          BIT(5)   /* 0: interrupt disabled, 1: interrupt enabled */
#define EXTCTL_CAPDBEN         BIT(6)   /* 0: TMx_EXT de-bounce disabled, 1: enabled */
#define EXTCTL_CNTDBEN         BIT(7)   /* 0: TMx de-bounce disabled, 1: enabled */
#define EXTCTL_INTERCAPSEL_SHIFT  8
#define EXTCTL_INTERCAPSEL_MASK   GENMASK(10, 8)
#define EXTCTL_INTERCAPSEL_HXT  ((0x10) << EXTCTL_INTERCAPSEL_SHIFT)
#define EXTCTL_INTERCAPSEL_LXT  ((0x11) << EXTCTL_INTERCAPSEL_SHIFT)
#define EXTCTL_CAPEDGE_SHIFT   12
#define EXTCTL_CAPEDGE_MASK    GENMASK(14, 12)
#define EXTCTL_CAPEDGE_SET(x)  ((x) << EXTCTL_CAPEDGE_SHIFT)
#define EXTCTL_ECNTSSEL_SHIFT  16
#define EXTCTL_ECNTSSEL_MASK   GENMASK(18, 16)
#define EXTCTL_ECNTSSEL_SET(x) ((x) << EXTCTL_ECNTSSEL_SHIFT)
#define EXTCTL_CAPDIVSCL_SHIFT 28
#define EXTCTL_CAPDIVSCL_MASK  GENMASK(31, 28)
#define EXTCTL_CAPDIVSCL_DIV1     (0x0 << EXTCTL_CAPDIVSCL_SHIFT)   /* Capture source / 1 */
#define EXTCTL_CAPDIVSCL_DIV2     (0x1 << EXTCTL_CAPDIVSCL_SHIFT)   /* Capture source / 2 */
#define EXTCTL_CAPDIVSCL_DIV4     (0x2 << EXTCTL_CAPDIVSCL_SHIFT)   /* Capture source / 4 */
#define EXTCTL_CAPDIVSCL_DIV8     (0x3 << EXTCTL_CAPDIVSCL_SHIFT)   /* Capture source / 8 */
#define EXTCTL_CAPDIVSCL_DIV16    (0x4 << EXTCTL_CAPDIVSCL_SHIFT)   /* Capture source / 16 */
#define EXTCTL_CAPDIVSCL_DIV32    (0x5 << EXTCTL_CAPDIVSCL_SHIFT)   /* Capture source / 32 */
#define EXTCTL_CAPDIVSCL_DIV64    (0x6 << EXTCTL_CAPDIVSCL_SHIFT)   /* Capture source / 64 */
#define EXTCTL_CAPDIVSCL_DIV128   (0x7 << EXTCTL_CAPDIVSCL_SHIFT)   /* Capture source / 128 */
#define EXTCTL_CAPDIVSCL_DIV256   (0x8 << EXTCTL_CAPDIVSCL_SHIFT)   /* Capture source / 256 */

#define EINTSTS_CAPIF          BIT(0)   /* 0: no external capture interrupt, 1: interrupt occurred, write 1 to clear */
#define EINTSTS_CAPIFOV        BIT(1)   /* 0: no overrun, 1: capture latch happened when CAPIF=1, read-only */

#define TRGCTL_TRGSSEL      BIT(0)   /* 0: time-out interrupt triggers PDMA/EADC, 1: capture interrupt triggers PDMA/EADC */
#define TRGCTL_TRGEADC      BIT(2)   /* 0: disable EADC trigger, 1: enable EADC trigger */
#define TRGCTL_TRGPDMA      BIT(4)   /* 0: disable PDMA trigger, 1: enable PDMA trigger */

#define CAPNF_CAPNFEN           BIT(0)   /* 0: filter disabled, 1: filter enabled */

#define CAPNF_CAPNFSEL_SHIFT    4
#define CAPNF_CAPNFSEL_MASK     GENMASK(6, 4)
#define CAPNF_CAPNFSEL_ECLK        (0x0 << CAPNF_CAPNFSEL_SHIFT)
#define CAPNF_CAPNFSEL_ECLK_DIV2   (0x1 << CAPNF_CAPNFSEL_SHIFT)
#define CAPNF_CAPNFSEL_ECLK_DIV4   (0x2 << CAPNF_CAPNFSEL_SHIFT)
#define CAPNF_CAPNFSEL_ECLK_DIV8   (0x3 << CAPNF_CAPNFSEL_SHIFT)
#define CAPNF_CAPNFSEL_ECLK_DIV16  (0x4 << CAPNF_CAPNFSEL_SHIFT)
#define CAPNF_CAPNFSEL_ECLK_DIV32  (0x5 << CAPNF_CAPNFSEL_SHIFT)
#define CAPNF_CAPNFSEL_ECLK_DIV64  (0x6 << CAPNF_CAPNFSEL_SHIFT)
#define CAPNF_CAPNFSEL_ECLK_DIV128 (0x7 << CAPNF_CAPNFSEL_SHIFT)

#define CAPNF_CAPNFCNT_SHIFT     8
#define CAPNF_CAPNFCNT_MASK      GENMASK(10, 8)
#define CAPNF_CAPNFCNT_SET(x)    ((x) << CAPNF_CAPNFCNT_SHIFT)
/* Filter count: 0 ~ 7 */

#endif /*  __ASM_ARCH_REGS_TIMER_H */
