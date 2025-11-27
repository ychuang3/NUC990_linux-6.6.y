// SPDX-License-Identifier: GPL-2.0+
/*
 * Nuvoton NUC990 platform initialization
 *
 * Copyright (C) 2025 Nuvoton Technology Corp.
 *
 * Author: Joey Lu <yclu4@nuvoton.com>
 */

#include <linux/kernel.h>
#include <linux/io.h>
#include <linux/of_platform.h>
#include <linux/types.h>
#include <linux/sizes.h>
#include <asm/mach/arch.h>
#include <asm/mach/map.h>

#include <linux/serial_core.h>
#include <linux/serial.h>

#define NUC990_IO_VIRT_BASE		0xF0000000
#define NUC990_IO_PHYS_BASE		0xB0000000
#define NUC990_IO_SIZE			0x00140000

#define NUC990_ISRAM_VIRT_BASE	0xFC000000
#define NUC990_ISRAM_PHYS_BASE	0xBC000000
#define NUC990_ISRAM_SIZE		0x00008000

#define NUC990_EBI_VIRT_BASE	0xF0300000
#define NUC990_EBI_PHYS_BASE	0xE0000000
#define NUC990_EBI_SIZE			0x00300000

#define MAP_DESC_ENTRY(s) \
{ \
	.virtual = s##_VIRT_BASE, \
	.pfn     = __phys_to_pfn(s##_PHYS_BASE), \
	.length  = s##_SIZE, \
	.type    = MT_DEVICE \
}

#define UART_REG_DAT		(0x0000)
#define UART_REG_FSR		(0x0018)
#define TX_EMPTY			(0x00400000)

static void nuc990_early_console_putc(struct uart_port *port, unsigned char ch)
{
	void __iomem *base = port->membase;

	/* wait for TX empty */
	while (!(readl_relaxed(base + UART_REG_FSR) & TX_EMPTY))
		cpu_relax();

	writel_relaxed(ch, base + UART_REG_DAT);
}

static void nuc990_early_console_write(struct console *con, const char *s, unsigned count)
{
	struct earlycon_device *dev = con->data;

	uart_console_write(&dev->port, s, count, nuc990_early_console_putc);
}

static int __init nuc990_early_console_setup(struct earlycon_device *dev, const char *opt)
{
	if (!dev->port.membase)
		return -ENODEV;

	dev->con->write = nuc990_early_console_write;

	return 0;
}
OF_EARLYCON_DECLARE(nuc990_earlycon, "nuvoton,nuc990-earlycon", nuc990_early_console_setup);

static void __init nuc990_init_machine(void)
{
	/* dummy, do nothing */
}

static struct map_desc nuc990_io_desc[] __initdata = {
	MAP_DESC_ENTRY(NUC990_IO),
	MAP_DESC_ENTRY(NUC990_ISRAM),
	MAP_DESC_ENTRY(NUC990_EBI),
};

void __init nuc990_map_io(void)
{
	// static mapping before ioremap
	iotable_init(nuc990_io_desc, ARRAY_SIZE(nuc990_io_desc));
}

static const char *const nuc990_dt_compat[] = {
	"nuvoton,nuc990",
	NULL,
};

DT_MACHINE_START(NUC990_DT, "Nuvoton NUC990 SoC")
	.map_io = nuc990_map_io,
	.init_machine = nuc990_init_machine,
	.dt_compat = nuc990_dt_compat,
MACHINE_END
