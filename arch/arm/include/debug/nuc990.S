/* Early UART0 debug for NUC990 */

/* Physical base address of UART0 */
#define NUC990_UART0_PHYS	0xB0070000
#define NUC990_UART0_VIRT	0xF0070000

/* UART register offsets */
#define NUC990_UART0_DAT	0x00
#define NUC990_UART0_FIFOSTS	0x18
#define NUC990_UART0_TXEMPTY	(1 << 22)

		.macro	addruart, rp, rv, tmp
		ldr	\rp, =CONFIG_DEBUG_UART_PHYS	@ physical base
		ldr	\rv, =CONFIG_DEBUG_UART_VIRT	@ virtual base
		.endm

		.macro	senduart,rd,rx
		strb	\rd, [\rx, #(NUC990_UART0_DAT)]	@ TXDATA
		.endm

		.macro	waituartcts,rd,rx
		.endm

		.macro	waituarttxrdy,rd,rx
		.endm

		.macro	busyuart,rd,rx
1001:		ldr	\rd, [\rx, #(NUC990_UART0_FIFOSTS)]	@ Read Status Register
		tst	\rd, #NUC990_UART0_TXEMPTY		@ TX_EMPTY
		beq	1001b			@ wait until transmit done
		.endm
