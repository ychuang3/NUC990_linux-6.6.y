/* SPDX-License-Identifier: GPL-2.0 */
/*
 * Nuvoton NUC990 Timer driver
 *
 * Copyright (C) 2025 Nuvoton Technology Corp.
 */
#ifndef _NUC980_TIMER_H_
#define _NUC980_TIMER_H_

#include <linux/types.h>
#include <linux/ioctl.h>

#define TMR_IOC_MAGIC		'e'
#define TMR_IOC_MAXNR		8

struct nuc990_timer_config {
unsigned int clk_idx;// 0: HXT, 1: PCLK0_DIV2, 2: PCLK0_DIV4K, 3: LXT
unsigned int target_hz; //expect freq
};

#define TMR_IOC_STOP			_IO(TMR_IOC_MAGIC, 0)
#define TMR_IOC_TOGGLE			_IOW(TMR_IOC_MAGIC, 1, unsigned int *)
#define TMR_IOC_FREE_COUNTING		_IOW(TMR_IOC_MAGIC, 2, unsigned int *)
#define TMR_IOC_TRIGGER_COUNTING	_IOW(TMR_IOC_MAGIC, 3, unsigned int *)
#define TMR_IOC_PERIODIC		_IOW(TMR_IOC_MAGIC, 4, unsigned int *)
#define TMR_IOC_CLKLXT			_IOW(TMR_IOC_MAGIC, 5, unsigned int *)
#define TMR_IOC_CLKHXT			_IOW(TMR_IOC_MAGIC, 6, unsigned int *)
#define TMR_IOC_EVENT_COUNTING		_IOW(TMR_IOC_MAGIC, 7, unsigned int *)
#define TMR_IOC_CLKSET			_IOW(TMR_IOC_MAGIC, 8, struct nuc990_timer_config *)
// Valid parameters for capture mode ioctls
#define TMR_CAP_EDGE_FF			0x00000
#define TMR_CAP_EDGE_RR			0x40000
#define TMR_CAP_EDGE_FR			0x80000
#define TMR_CAP_EDGE_RF			0xC0000

#define TMR_EXTCNT_EDGE_RF		0x2000
#define TMR_EXTCNT_EDGE_FF		0x0000

#define __HXT	0
#define __PCLK	1
#define __PCLK_DIV4K 2
#define __LXT	3
#endif
