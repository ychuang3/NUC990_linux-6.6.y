// SPDX-License-Identifier: GPL-2.0
/*
 * Nuvoton NUC990 External Bus Interface(EBI) driver
 *
 * Copyright (C) 2025 Nuvoton Technology Corp.
 */
#ifndef _NUC990_EBI_H_
#define _NUC990_EBI_H_

#include <linux/types.h>
#include <linux/ioctl.h>

#define EBI_IOC_MAGIC		'e'
#define EBI_IOC_SET		_IOW(EBI_IOC_MAGIC, 0, unsigned int *)

struct nuc990_set_ebi {
	unsigned int bank;
	unsigned int CSActiveLevel;
	unsigned int base;
	unsigned int size;
	unsigned int width;
};

#endif /* _NUC990_EBI_H_ */
