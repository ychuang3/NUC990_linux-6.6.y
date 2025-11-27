// SPDX-License-Identifier: GPL-2.0
/*
 * Nuvoton NUC990 Serial driver
 *
 * Copyright (C) 2025 Nuvoton Technology Corp.
 *
 * Author: Joey Lu <yclu4@nuvoton.com>
 */

#include <linux/clk.h>
#include <linux/module.h>
#include <linux/ioport.h>
#include <linux/init.h>
#include <linux/console.h>
#include <linux/sysrq.h>
#include <linux/tty.h>
#include <linux/tty_flip.h>
#include <linux/serial_core.h>
#include <linux/serial.h>
#include <linux/platform_device.h>
#include <linux/delay.h>
#include <linux/io.h>
#include <linux/irq.h>
#include <linux/of.h>
#include <linux/of_address.h>
#include <linux/pm_runtime.h>

#define UART_REG_DAT	0x00
#define UART_REG_IER	0x04
#define RDA_IEN		0x00000001
#define THRE_IEN	0x00000002
#define RLS_IEN		0x00000004
#define RTO_IEN		0x00000010
#define BUFERR_IEN	0x00000020
#define TIME_OUT_EN	0x00000800
#define ATORTSEN	0x00001000
#define ATOCTSEN	0x00002000
#define TXPDMAEN	0x00004000
#define RXPDMAEN	0x00008000

#define UART_REG_FCR	0x08
#define RFR		0x00000002
#define TFR		0x00000004
#define UART_REG_LCR	0x0C
#define	NSB		0x00000004
#define PBE		0x00000008
#define EPE		0x00000010
#define SPE		0x00000020
#define BCB		0x00000040

#define UART_REG_MCR	0x10
#define UART_REG_MSR	0x14
#define UART_REG_FSR	0x18
#define RX_OVER_IF	0x00000001
#define TX_OVER_IF	0x01000000
#define PEF		0x00000010
#define FEF		0x00000020
#define BIF		0x00000040
#define RX_EMPTY	0x00004000
#define TX_EMPTY	0x00400000
#define TX_FULL		0x00800000
#define RX_FULL		0x00008000
#define TE_FLAG		0x10000000

#define UART_REG_ISR	0x1C
#define RDA_IF		0x00000001
#define THRE_IF		0x00000002
#define TOUT_IF		0x00000010
#define THRE_INT	0x00000200
#define HWRLS_IF	0x00040000
#define HWBUFE_IF	0x00200000

#define UART_REG_TOR	0x20
#define UART_REG_BAUD	0x24

#define UART_NR		10
#define DRIVER_NAME	"nuc990-serial"
#define to_nuc990_uart_port(u) container_of(u, struct uart_nuc990_port, port)

// 1 bit per uart
#define CLK_PCLKEN0_OFFSET	0x18
#define CLK_PCLKEN0_MASK	GENMASK(25, 16)
// 8 bits per uart
#define CLK_DIVCTL4_OFFSET	0x30
#define CLK_DIVCTL4_SOURCE_MASK	GENMASK(4, 3)
#define CLK_DIVCTL4_DIV_MASK	GENMASK(7, 5)

struct nuc990_uart_config {
	int id;
	int rx_ch;
	int tx_ch;
	int pdma_en;
	int wakeup_en;
	// other configs
};

struct uart_nuc990_port {
	struct uart_port	port;
	struct clk		*clk;

	unsigned short	capabilities;   /* port capabilities */
	unsigned char	ier;
	unsigned char	lcr;
	unsigned char	mcr;
	unsigned char	mcr_mask;  /* mask of user bits */
	unsigned char	mcr_force; /* mask of forced bits */

	struct serial_rs485	rs485; /* rs485 settings */

	struct nuc990_uart_config	config;

	int max_count;
};
static struct uart_nuc990_port nuc990_serial_ports[UART_NR];

static inline unsigned int serial_in(struct uart_nuc990_port *p, int offset)
{
	return __raw_readl(p->port.membase + offset);
}

static inline void serial_out(struct uart_nuc990_port *p, int offset, int value)
{
	__raw_writel(value, p->port.membase + offset);
}

static unsigned int nuc990_tx_empty(struct uart_port *port)
{
	struct uart_nuc990_port *up = to_nuc990_uart_port(port);
	unsigned int fsr;

	//spin_lock_irqsave(&up->port.lock, flags);
	fsr = serial_in(up, UART_REG_FSR);
	//spin_unlock_irqrestore(&up->port.lock, flags);

	return (fsr & (TE_FLAG | TX_EMPTY)) == (TE_FLAG | TX_EMPTY) ? TIOCSER_TEMT : 0;
}

static void nuc990_set_mctrl(struct uart_port *port, unsigned int mctrl)
{
	struct uart_nuc990_port *up = to_nuc990_uart_port(port);
	unsigned int mcr = 0;
	unsigned int ier = 0;

	if (mctrl & TIOCM_RTS) {
		// set RTS high level trigger
		mcr = serial_in(up, UART_REG_MCR);
		mcr |= 0x200;
		mcr &= ~(0x2);
	}

	if (up->mcr & UART_MCR_AFE) {
		// set RTS high level trigger
		mcr = serial_in(up, UART_REG_MCR);
		mcr |= 0x200;
		mcr &= ~(0x2);

		// enable CTS/RTS auto-flow control
		serial_out(up, UART_REG_IER, (serial_in(up, UART_REG_IER) | ATORTSEN | ATOCTSEN));

		// Set hardware flow control
		up->port.flags |= UPF_HARD_FLOW;
	} else {
		// disable CTS/RTS auto-flow control
		ier = serial_in(up, UART_REG_IER);
		ier &= ~(ATORTSEN | ATOCTSEN);
		serial_out(up, UART_REG_IER, ier);

		//un-set hardware flow control
		up->port.flags &= ~UPF_HARD_FLOW;
	}

	// set CTS high level trigger
	serial_out(up, UART_REG_MSR, (serial_in(up, UART_REG_MSR) | (0x100)));

	serial_out(up, UART_REG_MCR, mcr);
}

static unsigned int nuc990_get_mctrl(struct uart_port *port)
{
	struct uart_nuc990_port *up = to_nuc990_uart_port(port);
	unsigned int status;
	unsigned int ret = 0;

	status = serial_in(up, UART_REG_MSR);

	if(!(status & 0x10))
		ret |= TIOCM_CTS;

	return ret;
}

static void nuc990_start_tx(struct uart_port *port)
{
	struct uart_nuc990_port *up = to_nuc990_uart_port(port);
	/* Enable TX interrupt */
	serial_out(up, UART_REG_IER, serial_in(up, UART_REG_IER) | THRE_IEN);
}

static void nuc990_stop_tx(struct uart_port *port)
{
	struct uart_nuc990_port *up = to_nuc990_uart_port(port);
	serial_out(up, UART_REG_IER, serial_in(up, UART_REG_IER) & ~THRE_IEN);
}

static void nuc990_stop_rx(struct uart_port *port)
{
	struct uart_nuc990_port *up = to_nuc990_uart_port(port);
	serial_out(up, UART_REG_IER, serial_in(up, UART_REG_IER) & ~RDA_IEN);
}

static void transmit_chars(struct uart_nuc990_port *up)
{
	struct circ_buf *xmit = &up->port.state->xmit;
	int count = 16 -((serial_in(up, UART_REG_FSR)>>16)&0xF);

	if(serial_in(up, UART_REG_FSR) & TX_FULL){
		count = 0;
	}

	if (up->port.x_char) {
		while(serial_in(up, UART_REG_FSR) & TX_FULL);
		serial_out(up, UART_REG_DAT, up->port.x_char);
		up->port.icount.tx++;
		up->port.x_char = 0;
		return;
	}

	if (uart_tx_stopped(&up->port)) {
		nuc990_stop_tx(&up->port);
		return;
	}

	if (uart_circ_empty(xmit)) {
		nuc990_stop_tx(&up->port);
		return;
	}

	while(count > 0){
		//while(serial_in(up, UART_REG_FSR) & TX_FULL);
		serial_out(up, UART_REG_DAT, xmit->buf[xmit->tail]);
		xmit->tail = (xmit->tail + 1) & (UART_XMIT_SIZE - 1);
		up->port.icount.tx++;
		count--;
		if (uart_circ_empty(xmit))
			break;
	}

	if (uart_circ_chars_pending(xmit) < WAKEUP_CHARS)
		uart_write_wakeup(&up->port);

	if (uart_circ_empty(xmit))
		nuc990_stop_tx(&up->port);

}

static void receive_chars(struct uart_nuc990_port *up)
{
	unsigned char ch;
	unsigned int fsr;
	unsigned int isr;
	unsigned int dcnt;
	unsigned long flags;
	char flag;

	isr = serial_in(up, UART_REG_ISR);
	fsr = serial_in(up, UART_REG_FSR);

	while(!(fsr & RX_EMPTY)) {
		//fsr = serial_in(up, UART_REG_FSR);
		flag = TTY_NORMAL;
		up->port.icount.rx++;

		if (unlikely(fsr & (BIF | FEF | PEF | RX_OVER_IF))) {
			if (fsr & BIF) {
				serial_out(up, UART_REG_FSR, BIF);
				up->port.icount.brk++;
				if (uart_handle_break(&up->port))
					continue;
			}

			if (fsr & FEF) {
				serial_out(up, UART_REG_FSR, FEF);
				up->port.icount.frame++;
			}

			if (fsr & PEF) {
				serial_out(up, UART_REG_FSR, PEF);
				up->port.icount.parity++;
			}

			if (fsr & RX_OVER_IF) {
				serial_out(up, UART_REG_FSR, RX_OVER_IF);
				up->port.icount.overrun++;
			}
			// FIXME: check port->read_status_mask to determin report flags
			if (fsr & BIF)
				flag = TTY_BREAK;
			if (fsr & PEF)
				flag = TTY_PARITY;
			if (fsr & FEF)
				flag = TTY_FRAME;
		}

		ch = (unsigned char)serial_in(up, UART_REG_DAT);

		if (uart_handle_sysrq_char(&up->port, ch))
			continue;

		uart_insert_char(&up->port, fsr, RX_OVER_IF, ch, flag);
		up->max_count++;
		dcnt = (serial_in(up, UART_REG_FSR) >> 8) & 0x3f;
		if (up->max_count > 1023)
		{
			spin_lock_irqsave(&up->port.lock, flags);
			tty_flip_buffer_push(&up->port.state->port);
			spin_unlock_irqrestore(&up->port.lock, flags);
			up->max_count = 0;
			if ((isr & TOUT_IF) && (dcnt == 0))
				goto tout_end;
		}

		if (isr & RDA_IF) {
			if (dcnt == 1)
				return; // have remaining data, don't reset max_count
		}
		fsr = serial_in(up, UART_REG_FSR);
	}

	spin_lock_irqsave(&up->port.lock, flags);
	tty_flip_buffer_push(&up->port.state->port);
	spin_unlock_irqrestore(&up->port.lock, flags);
tout_end:
	up->max_count=0;
	return;
}

static irqreturn_t nuc990_serial_interrupt(int irq, void *dev_id)
{
	struct uart_nuc990_port *up = to_nuc990_uart_port(dev_id);
	unsigned int isr, fsr;

	isr = serial_in(up, UART_REG_ISR);
	fsr = serial_in(up, UART_REG_FSR);

	if (isr & (RDA_IF | TOUT_IF))
		receive_chars(up);

	if (isr & THRE_INT)
		transmit_chars(up);

	if(fsr & (BIF | FEF | PEF | RX_OVER_IF | TX_OVER_IF)) {
		serial_out(up, UART_REG_FSR, (BIF | FEF | PEF | RX_OVER_IF | TX_OVER_IF));
	}

	return IRQ_HANDLED;
}

static int nuc990_startup(struct uart_port *port)
{
	struct uart_nuc990_port *up = to_nuc990_uart_port(port);
	int retval;

	clk_prepare_enable(up->clk);

	/* Clear FIFOs */
	serial_out(up, UART_REG_FCR, TFR | RFR);

	/* Clear pending interrupts */
	serial_out(up, UART_REG_ISR, 0xFFFFFFFF);

	retval = request_irq(port->irq, nuc990_serial_interrupt, IRQF_NO_SUSPEND, DRIVER_NAME, port);
	if (retval) {
		pr_err("request irq failed...\n");
		return retval;
	}

	/* Now, initialize the UART, FIFO trigger level 4 byte, RTS trigger level 8 bytes */
	serial_out(up, UART_REG_FCR, serial_in(up, UART_REG_FCR) | 0x10 | 0x20000);
	serial_out(up, UART_REG_LCR, 0x7);	/* 8 bit */
	serial_out(up, UART_REG_TOR, 0x40);

	serial_out(up, UART_REG_IER, RTO_IEN | RDA_IEN | TIME_OUT_EN | BUFERR_IEN);

    return 0;
}

static void nuc990_shutdown(struct uart_port *port)
{
    struct uart_nuc990_port *up = to_nuc990_uart_port(port);

    /* Disable all IRQs */
    serial_out(up, UART_REG_IER, 0);

    /* Turn off clock */
    clk_disable_unprepare(up->clk);
}

static unsigned int nuc990_serial_get_divisor(struct uart_port *port, unsigned int baud)
{
	unsigned int quot;

	quot = (port->uartclk / baud) - 2;

	return quot;
}

static void nuc990_set_termios(struct uart_port *port,
                               struct ktermios *new,
                               const struct ktermios *old)
{
    struct uart_nuc990_port *up = to_nuc990_uart_port(port);
	unsigned int lcr = 0;
	unsigned long flags;
	unsigned int baud, quot;

	switch (new->c_cflag & CSIZE) {
	case CS5:
		lcr = 0;
		break;
	case CS6:
		lcr |= 1;
		break;
	case CS7:
		lcr |= 2;
		break;
	default:
	case CS8:
		lcr |= 3;
		break;
	}

	if (new->c_cflag & CSTOPB)
		lcr |= NSB;
	if (new->c_cflag & PARENB)
		lcr |= PBE;
	if (!(new->c_cflag & PARODD))
		lcr |= EPE;
	if (new->c_cflag & CMSPAR)
		lcr |= SPE;

	baud = uart_get_baud_rate(port, new, old, port->uartclk / 0xffff, port->uartclk / 11);

	quot = nuc990_serial_get_divisor(port, baud);

	/*
	 * Ok, we're now changing the port state.  Do it with
	 * interrupts disabled.
	 */
	spin_lock_irqsave(&up->port.lock, flags);

	up->port.read_status_mask = RX_OVER_IF /*| UART_LSR_THRE | UART_LSR_DR*/;
	if (new->c_iflag & INPCK)
		up->port.read_status_mask |= FEF | PEF;
	if (new->c_iflag & (BRKINT | PARMRK))
		up->port.read_status_mask |= BIF;

	/*
	 * Characteres to ignore
	 */
	up->port.ignore_status_mask = 0;
	if (new->c_iflag & IGNPAR)
		up->port.ignore_status_mask |= FEF | PEF;
	if (new->c_iflag & IGNBRK) {
		up->port.ignore_status_mask |= BIF;
		/*
		 * If we're ignoring parity and break indicators,
		 * ignore overruns too (for real raw support).
		 */
		if (new->c_iflag & IGNPAR)
			up->port.ignore_status_mask |= RX_OVER_IF;
	}

	if (new->c_cflag & CRTSCTS)
		up->mcr |= UART_MCR_AFE;
	else
		up->mcr &= ~UART_MCR_AFE;

	nuc990_set_mctrl(&up->port, up->port.mctrl);

	serial_out(up, UART_REG_BAUD, quot | 0x30000000);

	serial_out(up, UART_REG_LCR, lcr);

	spin_unlock_irqrestore(&up->port.lock, flags);
}

static const char *nuc990_type(struct uart_port *port)
{
    return "nuc990-uart";
}

static void nuc990_config_port(struct uart_port *port, int flags)
{
	if (flags & UART_CONFIG_TYPE)
		port->type = PORT_NPCM;
}

static struct uart_ops nuc990_serial_ops = {
	.tx_empty    = nuc990_tx_empty,
	.set_mctrl   = nuc990_set_mctrl,
	.get_mctrl   = nuc990_get_mctrl,
	.stop_tx     = nuc990_stop_tx,
	.start_tx    = nuc990_start_tx,
	.stop_rx     = nuc990_stop_rx,
	.startup     = nuc990_startup,
	.shutdown    = nuc990_shutdown,
	.set_termios = nuc990_set_termios,
	.type        = nuc990_type,
	.config_port = nuc990_config_port,
};

#ifdef CONFIG_SERIAL_NUC990_CONSOLE
static void __maybe_unused nuc990_serial_console_putchar(struct uart_port *port, unsigned char ch)
{
	struct uart_nuc990_port *up = to_nuc990_uart_port(port);

	do {
	} while (!(serial_in(up, UART_REG_FSR) & TX_EMPTY));
	serial_out(up, UART_REG_DAT, ch);
}

/*
 *  Print a string to the serial port trying not to disturb
 *  any possible real use of the port...
 *
 *  The console_lock must be held when we get here.
 */
static void nuc990_serial_console_write(struct console *co, const char *s, unsigned int count)
{
	struct uart_nuc990_port *up;
	int idx = co->index;
	unsigned long flags;
	unsigned int ier;

	if (idx < 0 || idx >= UART_NR)
        return;

	up = &nuc990_serial_ports[idx];

	spin_lock_irqsave(&up->port.lock, flags);

	/*
	 *  First save the IER then disable the interrupts
	 */
	ier = serial_in(up, UART_REG_IER);
	serial_out(up, UART_REG_IER, 0);

	uart_console_write(&up->port, s, count, nuc990_serial_console_putchar);
	// wait_for_xmit_empty(&up->port);

	/*
	 *  Finally, wait for transmitter to become empty
	 *  and restore the IER
	 */
	do {
	} while (!(serial_in(up, UART_REG_FSR) & TX_EMPTY));
	serial_out(up, UART_REG_IER, ier);

	spin_unlock_irqrestore(&up->port.lock, flags);
}

static int __init nuc990_serial_console_setup(struct console *co, char *options)
{
	struct uart_port *port;
	int baud = 9600;
	int bits = 8;
	int parity = 'n';
	int flow = 'n';

	/*
	 * Check whether an invalid uart number has been specified, and
	 * if so, search for the first available port that does have
	 * console support.
	 */
	if (co->index >= UART_NR || co->index == -1)
		co->index = 0;
	port = &nuc990_serial_ports[co->index].port;
	
	if (!port) {
		pr_err("nuc990_serial_console_setup: no port found\n");
		return -ENODEV;
	}

	if (!port->membase)
	{
		pr_err("serial port %d not yet available\n", co->index);
		return -ENODEV;
	}

	if (options)
		uart_parse_options(options, &baud, &parity, &bits, &flow);

	return uart_set_options(port, co, baud, parity, bits, flow);
}

static struct uart_driver nuc990_uart_reg;
static struct console nuc990_serial_console = {
	.name = "ttyS",
	.write = nuc990_serial_console_write,
	.device = uart_console_device,
	.setup = nuc990_serial_console_setup,
	.flags = CON_PRINTBUFFER | CON_ENABLED,
	.index = -1,
	.data = &nuc990_uart_reg,
};
#define NUC990_SERIAL_CONSOLE    (&nuc990_serial_console)
#else
#define NUC990_SERIAL_CONSOLE    NULL
#endif

static struct uart_driver nuc990_uart_reg = {
	.owner		= THIS_MODULE,
	.driver_name	= DRIVER_NAME,
	.dev_name	= "ttyS",
	.major		= TTY_MAJOR,
	.minor		= 64,
	.nr		= UART_NR,
	.cons		= NUC990_SERIAL_CONSOLE,
};
static int serial_ports_sn = 0;

static int nuc990_serial_probe(struct platform_device *pdev)
{
	struct uart_nuc990_port *up;
	struct uart_port *port;
	struct resource *res;
	int irq, ret;
	u32 arg[4];

	res = platform_get_resource(pdev, IORESOURCE_MEM, 0);
	if (!res) {
		dev_err(&pdev->dev, "memory resource not found");
		return -EINVAL;
	}

	if (serial_ports_sn >= UART_NR) {
		pr_err("nuc990_serial: no more uart ports available\n");
		return -ENODEV;
	}

	irq = platform_get_irq(pdev, 0);
	if (irq < 0)
		return irq;

	up = &nuc990_serial_ports[serial_ports_sn];
	memset(up, 0, sizeof(*up));

	port = &up->port;
	port->iotype = UPIO_MEM;
	port->flags = UPF_BOOT_AUTOCONF;
	port->ops = &nuc990_serial_ops;
	port->dev = &pdev->dev;
	port->irq = irq;
	port->line = serial_ports_sn++;
	spin_lock_init(&port->lock);

	port->mapbase = res->start;
	port->iobase = res->start;
	port->membase = devm_ioremap(&pdev->dev, res->start,
					 resource_size(res));
	if (!port->membase)
		return -ENOMEM;

	up->clk = devm_clk_get(&pdev->dev, NULL);
	if (IS_ERR(up->clk)) {
		pr_err("nuc990_serial: failed to get clock\n");
		return PTR_ERR(up->clk);
	}

	ret = clk_prepare_enable(up->clk);
	if (ret) {
		pr_err("nuc990_serial: failed to enable clock\n");
		return ret;
	}

	port->uartclk = clk_get_rate(up->clk);
	if (!port->uartclk) {
		pr_err("nuc990_serial: failed to get clock rate\n");
		return -EINVAL;
	}

	port->rs485_config = NULL;

	if (of_property_read_u32_array(pdev->dev.of_node, "pdma-enable", arg, 1) != 0) {
		up->config.pdma_en = 0;
	} else {
		up->config.pdma_en = 1;
	}

	// request for dma...

	if (of_property_read_u32_array(pdev->dev.of_node, "wakeup-enable", arg, 1) != 0) {
		up->config.wakeup_en = 0;
	} else {
		if (arg[0] != 0)
			up->config.wakeup_en = 1;
	}

	ret = uart_add_one_port(&nuc990_uart_reg, &up->port);
	if (ret) {
		pr_err("nuc990_serial: uart_add_one_port failed\n");
		return ret;
	}

	platform_set_drvdata(pdev, port);

	return 0;
}

/*
 * Remove serial ports registered against a platform device.
 */
static int nuc990_serial_remove(struct platform_device *dev)
{

	return 0;
}

static int nuc990_serial_suspend(struct platform_device *dev, pm_message_t state)
{

	return 0;
}

static int nuc990_serial_resume(struct platform_device *dev)
{

	return 0;
}

static const struct of_device_id nuc990_serial_of_match[] = {
	{ .compatible = "nuvoton,nuc990-uart" },
	{ /* sentinel */ },
};
MODULE_DEVICE_TABLE(of, nuc990_serial_of_match);

static struct platform_driver nuc990_serial_driver = {
	.probe = nuc990_serial_probe,
	.remove = nuc990_serial_remove,
	.suspend = nuc990_serial_suspend,
	.resume = nuc990_serial_resume,
	.driver = {
		.name = DRIVER_NAME,
		.owner = THIS_MODULE,
		.of_match_table = of_match_ptr(nuc990_serial_of_match),
	},
};

static int __init nuc990_serial_init(void)
{
	int ret;

	ret = uart_register_driver(&nuc990_uart_reg);
	if (ret)
		return ret;

	ret = platform_driver_register(&nuc990_serial_driver);
	if (ret)
		uart_unregister_driver(&nuc990_uart_reg);

	return ret;
}

static void __exit nuc990_serial_exit(void)
{
	platform_driver_unregister(&nuc990_serial_driver);
	uart_unregister_driver(&nuc990_uart_reg);
}

module_init(nuc990_serial_init);
module_exit(nuc990_serial_exit);

MODULE_LICENSE("GPL");
MODULE_DESCRIPTION("NUC990 serial driver");

MODULE_ALIAS_CHARDEV_MAJOR(TTY_MAJOR);
