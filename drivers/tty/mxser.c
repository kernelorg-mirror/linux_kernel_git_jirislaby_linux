// SPDX-License-Identifier: GPL-2.0+
/*
 *          mxser.c  -- MOXA Smartio/Industio family multiport serial driver.
 *
 *      Copyright (C) 1999-2006  Moxa Technologies (support@moxa.com).
 *	Copyright (C) 2006-2008  Jiri Slaby <jirislaby@gmail.com>
 *
 *      This code is loosely based on the 1.8 moxa driver which is based on
 *	Linux serial driver, written by Linus Torvalds, Theodore T'so and
 *	others.
 *
 *	Fed through a cleanup, indent and remove of non 2.6 code by Alan Cox
 *	<alan@lxorguk.ukuu.org.uk>. The original 1.8 code is available on
 *	www.moxa.com.
 *	- Fixed x86_64 cleanness
 */

#include <linux/bitfield.h>
#include <linux/module.h>
#include <linux/errno.h>
#include <linux/signal.h>
#include <linux/sched.h>
#include <linux/timer.h>
#include <linux/interrupt.h>
#include <linux/tty.h>
#include <linux/tty_flip.h>
#include <linux/serial.h>
#include <linux/serial_core.h>
#include <linux/serial_reg.h>
#include <linux/major.h>
#include <linux/string.h>
#include <linux/fcntl.h>
#include <linux/ptrace.h>
#include <linux/ioport.h>
#include <linux/mm.h>
#include <linux/delay.h>
#include <linux/pci.h>
#include <linux/bitops.h>
#include <linux/slab.h>
#include <linux/ratelimit.h>

#include <asm/io.h>
#include <asm/irq.h>
#include <linux/uaccess.h>

/*
 *	Semi-public control interfaces
 */

/*
 *	MOXA ioctls
 */

#define MOXA			0x400
#define MOXA_SET_OP_MODE	(MOXA + 66)
#define MOXA_GET_OP_MODE	(MOXA + 67)

#define RS232_MODE		0
#define RS485_2WIRE_MODE	1
#define RS422_MODE		2
#define RS485_4WIRE_MODE	3
#define OP_MODE_MASK		3

/* --------------------------------------------------- */

/*
 * Follow just what Moxa Must chip defines.
 *
 * When LCR register (offset 0x03) is written the following value, the Must chip
 * will enter enhanced mode. And a write to EFR (offset 0x02) bit 6,7 will
 * change bank.
 */
#define MOXA_MUST_ENTER_ENHANCED	0xBF

/* when enhanced mode is enabled, access to general bank register */
#define MOXA_MUST_GDL_REGISTER		0x07
#define MOXA_MUST_GDL_MASK		0x7F
#define MOXA_MUST_GDL_HAS_BAD_DATA	0x80

#define MOXA_MUST_LSR_RERR		0x80	/* error in receive FIFO */
/* enhanced register bank select and enhanced mode setting register */
/* This works only when LCR register equals to 0xBF */
#define MOXA_MUST_EFR_REGISTER		0x02
#define MOXA_MUST_EFR_EFRB_ENABLE	0x10 /* enhanced mode enable */
/* enhanced register bank set 0, 1, 2 */
#define MOXA_MUST_EFR_BANK0		0x00
#define MOXA_MUST_EFR_BANK1		0x40
#define MOXA_MUST_EFR_BANK2		0x80
#define MOXA_MUST_EFR_BANK3		0xC0
#define MOXA_MUST_EFR_BANK_MASK		0xC0

/* set XON1 value register, when LCR=0xBF and change to bank0 */
#define MOXA_MUST_XON1_REGISTER		0x04

/* set XON2 value register, when LCR=0xBF and change to bank0 */
#define MOXA_MUST_XON2_REGISTER		0x05

/* set XOFF1 value register, when LCR=0xBF and change to bank0 */
#define MOXA_MUST_XOFF1_REGISTER	0x06

/* set XOFF2 value register, when LCR=0xBF and change to bank0 */
#define MOXA_MUST_XOFF2_REGISTER	0x07

#define MOXA_MUST_RBRTL_REGISTER	0x04
#define MOXA_MUST_RBRTH_REGISTER	0x05
#define MOXA_MUST_RBRTI_REGISTER	0x06
#define MOXA_MUST_THRTL_REGISTER	0x07
#define MOXA_MUST_ENUM_REGISTER		0x04
#define MOXA_MUST_HWID_REGISTER		0x05
#define MOXA_MUST_ECR_REGISTER		0x06
#define MOXA_MUST_CSR_REGISTER		0x07

#define MOXA_MUST_FCR_GDA_MODE_ENABLE	0x20 /* good data mode enable */
#define MOXA_MUST_FCR_GDA_ONLY_ENABLE	0x10 /* only good data put into RxFIFO */

#define MOXA_MUST_IER_ECTSI		0x80 /* enable CTS interrupt */
#define MOXA_MUST_IER_ERTSI		0x40 /* enable RTS interrupt */
#define MOXA_MUST_IER_XINT		0x20 /* enable Xon/Xoff interrupt */
#define MOXA_MUST_IER_EGDAI		0x10 /* enable GDA interrupt */

#define MOXA_MUST_RECV_ISR		(UART_IER_RDI | MOXA_MUST_IER_EGDAI)

/* GDA interrupt pending */
#define MOXA_MUST_IIR_GDA		0x1C
#define MOXA_MUST_IIR_RDA		0x04
#define MOXA_MUST_IIR_RTO		0x0C
#define MOXA_MUST_IIR_LSR		0x06

/* received Xon/Xoff or specical interrupt pending */
#define MOXA_MUST_IIR_XSC		0x10

/* RTS/CTS change state interrupt pending */
#define MOXA_MUST_IIR_RTSCTS		0x20
#define MOXA_MUST_IIR_MASK		0x3E

#define MOXA_MUST_MCR_XON_FLAG		0x40
#define MOXA_MUST_MCR_XON_ANY		0x80
#define MOXA_MUST_MCR_TX_XON		0x08

#define MOXA_MUST_EFR_SF_MASK		0x0F /* software flow control on chip mask value */
#define MOXA_MUST_EFR_SF_TX1		0x08 /* send Xon1/Xoff1 */
#define MOXA_MUST_EFR_SF_TX2		0x04 /* send Xon2/Xoff2 */
#define MOXA_MUST_EFR_SF_TX12		0x0C /* send Xon1,Xon2/Xoff1,Xoff2 */
#define MOXA_MUST_EFR_SF_TX_NO		0x00 /* don't send Xon/Xoff */
#define MOXA_MUST_EFR_SF_TX_MASK	0x0C /* Tx software flow control mask */
#define MOXA_MUST_EFR_SF_RX_NO		0x00 /* don't receive Xon/Xoff */
#define MOXA_MUST_EFR_SF_RX1		0x02 /* receive Xon1/Xoff1 */
#define MOXA_MUST_EFR_SF_RX2		0x01 /* receive Xon2/Xoff2 */
#define MOXA_MUST_EFR_SF_RX12		0x03 /* receive Xon1,Xon2/Xoff1,Xoff2 */
#define MOXA_MUST_EFR_SF_RX_MASK	0x03 /* Rx software flow control mask */

#define	MXSERMAJOR	 174

#define MXSER_BOARDS		4	/* Max. boards */
#define MXSER_PORTS_PER_BOARD	8	/* Max. ports per board */
#define MXSER_PORTS		(MXSER_BOARDS * MXSER_PORTS_PER_BOARD)
#define MXSER_ISR_PASS_LIMIT	100

#define WAKEUP_CHARS		256

#define MXSER_BAUD_BASE		921600
#define MXSER_CUSTOM_DIVISOR	(MXSER_BAUD_BASE * 16)

#define PCI_DEVICE_ID_MOXA_RC7000	0x0001
#define PCI_DEVICE_ID_MOXA_CP102	0x1020
#define PCI_DEVICE_ID_MOXA_CP102UL	0x1021
#define PCI_DEVICE_ID_MOXA_CP102U	0x1022
#define PCI_DEVICE_ID_MOXA_CP102UF	0x1023
#define PCI_DEVICE_ID_MOXA_C104		0x1040
#define PCI_DEVICE_ID_MOXA_CP104U	0x1041
#define PCI_DEVICE_ID_MOXA_CP104JU	0x1042
#define PCI_DEVICE_ID_MOXA_CP104EL	0x1043
#define PCI_DEVICE_ID_MOXA_POS104UL	0x1044
#define PCI_DEVICE_ID_MOXA_CB108	0x1080
#define PCI_DEVICE_ID_MOXA_CP112UL	0x1120
#define PCI_DEVICE_ID_MOXA_CT114	0x1140
#define PCI_DEVICE_ID_MOXA_CP114	0x1141
#define PCI_DEVICE_ID_MOXA_CB114	0x1142
#define PCI_DEVICE_ID_MOXA_CP114UL	0x1143
#define PCI_DEVICE_ID_MOXA_CP118U	0x1180
#define PCI_DEVICE_ID_MOXA_CP118EL	0x1181
#define PCI_DEVICE_ID_MOXA_CP132	0x1320
#define PCI_DEVICE_ID_MOXA_CP132U	0x1321
#define PCI_DEVICE_ID_MOXA_CP134U	0x1340
#define PCI_DEVICE_ID_MOXA_CB134I	0x1341
#define PCI_DEVICE_ID_MOXA_CP138U	0x1380
#define PCI_DEVICE_ID_MOXA_C168		0x1680
#define PCI_DEVICE_ID_MOXA_CP168U	0x1681
#define PCI_DEVICE_ID_MOXA_CP168EL	0x1682

#define MXSER_HIGHBAUD			0x0100

enum mxser_must_hwid {
	MOXA_OTHER_UART		= 0x00,
	MOXA_MUST_MU150_HWID	= 0x01,
	MOXA_MUST_MU860_HWID	= 0x02,
};

static const struct {
	u8 type;
	u8 fifo_size;
	u8 rx_high_water;
	u8 rx_low_water;
	speed_t max_baud;
} Gpci_uart_info[] = {
	{ MOXA_OTHER_UART,	 16, 14,  1, 921600 },
	{ MOXA_MUST_MU150_HWID,	 64, 48, 16, 230400 },
	{ MOXA_MUST_MU860_HWID, 128, 96, 32, 921600 }
};
#define UART_INFO_NUM	ARRAY_SIZE(Gpci_uart_info)

static const struct pci_device_id mxser_pcibrds[] = {
	{ PCI_DEVICE_DATA(MOXA, C168, 0) },
	{ PCI_DEVICE_DATA(MOXA, C104, 0) },
	{ PCI_DEVICE_DATA(MOXA, CP132, 0) },
	{ PCI_DEVICE_DATA(MOXA, CP114, 0) },
	{ PCI_DEVICE_DATA(MOXA, CT114, 0) },
	{ PCI_DEVICE_DATA(MOXA, CP102, MXSER_HIGHBAUD) },
	{ PCI_DEVICE_DATA(MOXA, CP104U, 0) },
	{ PCI_DEVICE_DATA(MOXA, CP168U, 0) },
	{ PCI_DEVICE_DATA(MOXA, CP132U, 0) },
	{ PCI_DEVICE_DATA(MOXA, CP134U, 0) },
	{ PCI_DEVICE_DATA(MOXA, CP104JU, 0) },
	{ PCI_DEVICE_DATA(MOXA, RC7000, 0) }, /* RC7000 */
	{ PCI_DEVICE_DATA(MOXA, CP118U, 0) },
	{ PCI_DEVICE_DATA(MOXA, CP102UL, 0) },
	{ PCI_DEVICE_DATA(MOXA, CP102U, 0) },
	{ PCI_DEVICE_DATA(MOXA, CP118EL, 0) },
	{ PCI_DEVICE_DATA(MOXA, CP168EL, 0) },
	{ PCI_DEVICE_DATA(MOXA, CP104EL, 0) },
	{ PCI_DEVICE_DATA(MOXA, CB108, 0) },
	{ PCI_DEVICE_DATA(MOXA, CB114, 0) },
	{ PCI_DEVICE_DATA(MOXA, CB134I, 0) },
	{ PCI_DEVICE_DATA(MOXA, CP138U, 0) },
	{ PCI_DEVICE_DATA(MOXA, POS104UL, 0) },
	{ PCI_DEVICE_DATA(MOXA, CP114UL, 0) },
	{ PCI_DEVICE_DATA(MOXA, CP102UF, 0) },
	{ PCI_DEVICE_DATA(MOXA, CP112UL, 0) },
	{ }
};
MODULE_DEVICE_TABLE(pci, mxser_pcibrds);

static int ttymajor = MXSERMAJOR;

/* Variables for insmod */

MODULE_AUTHOR("Casper Yang");
MODULE_DESCRIPTION("MOXA Smartio/Industio Family Multiport Board Device Driver");
module_param(ttymajor, int, 0);
MODULE_LICENSE("GPL");

struct mxser_board;

struct mxser_port {
	struct uart_port uport;
	struct mxser_board *board;

	unsigned long opmode_ioaddr;

	u8 rx_high_water;
	u8 rx_low_water;

	u8 IER;			/* Interrupt Enable Register */
	u8 MCR;			/* Modem control register */
	u8 FCR;			/* FIFO control register */
};

struct mxser_board {
	unsigned int idx;
	unsigned short nports;
	unsigned long vector;

	enum mxser_must_hwid must_hwid;
	speed_t max_baud;

	struct mxser_port ports[] /* __counted_by(nports) */;
};

static DECLARE_BITMAP(mxser_boards, MXSER_BOARDS);

static inline struct mxser_port *to_mport(struct uart_port *uport)
{
	return container_of(uport, struct mxser_port, uport);
}

static u8 __mxser_must_set_EFR(unsigned long baseio, u8 clear, u8 set,
		bool restore_LCR)
{
	u8 oldlcr, efr;

	oldlcr = inb(baseio + UART_LCR);
	outb(MOXA_MUST_ENTER_ENHANCED, baseio + UART_LCR);

	efr = inb(baseio + MOXA_MUST_EFR_REGISTER);
	efr &= ~clear;
	efr |= set;

	outb(efr, baseio + MOXA_MUST_EFR_REGISTER);

	if (restore_LCR)
		outb(oldlcr, baseio + UART_LCR);

	return oldlcr;
}

static u8 mxser_must_select_bank(unsigned long baseio, u8 bank)
{
	return __mxser_must_set_EFR(baseio, MOXA_MUST_EFR_BANK_MASK, bank,
			false);
}

static void mxser_set_must_xon1_value(unsigned long baseio, u8 value)
{
	u8 oldlcr = mxser_must_select_bank(baseio, MOXA_MUST_EFR_BANK0);
	outb(value, baseio + MOXA_MUST_XON1_REGISTER);
	outb(oldlcr, baseio + UART_LCR);
}

static void mxser_set_must_xoff1_value(unsigned long baseio, u8 value)
{
	u8 oldlcr = mxser_must_select_bank(baseio, MOXA_MUST_EFR_BANK0);
	outb(value, baseio + MOXA_MUST_XOFF1_REGISTER);
	outb(oldlcr, baseio + UART_LCR);
}

static void mxser_set_must_fifo_value(struct mxser_port *info)
{
	struct uart_port *uport = &info->uport;

	u8 oldlcr = mxser_must_select_bank(uport->iobase, MOXA_MUST_EFR_BANK1);
	outb(info->rx_high_water, uport->iobase + MOXA_MUST_RBRTH_REGISTER);
	outb(info->rx_high_water, uport->iobase + MOXA_MUST_RBRTI_REGISTER);
	outb(info->rx_low_water, uport->iobase + MOXA_MUST_RBRTL_REGISTER);
	outb(oldlcr, uport->iobase + UART_LCR);
}

static void mxser_set_must_enum_value(unsigned long baseio, u8 value)
{
	u8 oldlcr = mxser_must_select_bank(baseio, MOXA_MUST_EFR_BANK2);
	outb(value, baseio + MOXA_MUST_ENUM_REGISTER);
	outb(oldlcr, baseio + UART_LCR);
}

static u8 mxser_get_must_hardware_id(unsigned long baseio)
{
	u8 oldlcr = mxser_must_select_bank(baseio, MOXA_MUST_EFR_BANK2);
	u8 id = inb(baseio + MOXA_MUST_HWID_REGISTER);
	outb(oldlcr, baseio + UART_LCR);

	return id;
}

static void mxser_must_set_EFR(unsigned long baseio, u8 clear, u8 set)
{
	__mxser_must_set_EFR(baseio, clear, set, true);
}

static void mxser_must_set_enhance_mode(unsigned long baseio, bool enable)
{
	mxser_must_set_EFR(baseio,
			enable ? 0 : MOXA_MUST_EFR_EFRB_ENABLE,
			enable ? MOXA_MUST_EFR_EFRB_ENABLE : 0);
}

static void mxser_must_no_sw_flow_control(unsigned long baseio)
{
	mxser_must_set_EFR(baseio, MOXA_MUST_EFR_SF_MASK, 0);
}

static void mxser_must_set_tx_sw_flow_control(unsigned long baseio, bool enable)
{
	mxser_must_set_EFR(baseio, MOXA_MUST_EFR_SF_TX_MASK,
			enable ? MOXA_MUST_EFR_SF_TX1 : 0);
}

static void mxser_must_set_rx_sw_flow_control(unsigned long baseio, bool enable)
{
	mxser_must_set_EFR(baseio, MOXA_MUST_EFR_SF_RX_MASK,
			enable ? MOXA_MUST_EFR_SF_RX1 : 0);
}

static enum mxser_must_hwid mxser_must_get_hwid(unsigned long io)
{
	u8 oldmcr, hwid;
	int i;

	outb(0, io + UART_LCR);
	mxser_must_set_enhance_mode(io, false);
	oldmcr = inb(io + UART_MCR);
	outb(0, io + UART_MCR);
	mxser_set_must_xon1_value(io, 0x11);
	if (inb(io + UART_MCR) != 0) {
		outb(oldmcr, io + UART_MCR);
		return MOXA_OTHER_UART;
	}

	hwid = mxser_get_must_hardware_id(io);
	for (i = 1; i < UART_INFO_NUM; i++) /* 0 = OTHER_UART */
		if (hwid == Gpci_uart_info[i].type)
			return hwid;

	return MOXA_OTHER_UART;
}

static bool mxser_16550A_or_MUST(struct mxser_port *info)
{
	struct uart_port *uport = &info->uport;

	return uport->type == PORT_16550A || info->board->must_hwid;
}

static void mxser_process_txrx_fifo(struct mxser_port *info)
{
	struct uart_port *uport = &info->uport;
	unsigned int i;

	if (uport->type == PORT_16450 || uport->type == PORT_8250) {
		info->rx_high_water = 1;
		info->rx_low_water = 1;
		uport->fifosize = 1;
		return;
	}

	for (i = 0; i < UART_INFO_NUM; i++)
		if (info->board->must_hwid == Gpci_uart_info[i].type) {
			info->rx_low_water = Gpci_uart_info[i].rx_low_water;
			info->rx_high_water = Gpci_uart_info[i].rx_high_water;
			uport->fifosize = Gpci_uart_info[i].fifo_size;
			break;
		}
}

static void mxser_start_tx(struct uart_port *uport)
{
	struct mxser_port *info = to_mport(uport);

	outb(info->IER & ~UART_IER_THRI, uport->iobase + UART_IER);
	info->IER |= UART_IER_THRI;
	outb(info->IER, uport->iobase + UART_IER);
}

static void mxser_stop_tx(struct uart_port *uport)
{
	struct mxser_port *info = to_mport(uport);

	info->IER &= ~UART_IER_THRI;
	outb(info->IER, uport->iobase + UART_IER);
}

static int mxser_set_baud(struct mxser_port *info, struct ktermios *termios)
{
	struct uart_port *uport = &info->uport;
	unsigned int quot = 0, baud;
	unsigned char cval;
	speed_t newspd = tty_termios_baud_rate(termios);

	if (newspd > info->board->max_baud)
		return -1;

	if (newspd == 134) {
		quot = 2 * MXSER_BAUD_BASE / 269;
		tty_termios_encode_baud_rate(termios, 134, 134);
	} else if (newspd) {
		quot = MXSER_BAUD_BASE / newspd;
		if (quot == 0)
			quot = 1;
		baud = MXSER_BAUD_BASE / quot;
		tty_termios_encode_baud_rate(termios, baud, baud);
	} else {
		quot = 0;
	}

	uart_update_timeout(uport, termios->c_cflag, MXSER_BAUD_BASE);

	if (quot) {
		info->MCR |= UART_MCR_DTR;
		outb(info->MCR, uport->iobase + UART_MCR);
	} else {
		info->MCR &= ~UART_MCR_DTR;
		outb(info->MCR, uport->iobase + UART_MCR);
		return 0;
	}

	cval = inb(uport->iobase + UART_LCR);

	outb(cval | UART_LCR_DLAB, uport->iobase + UART_LCR);	/* set DLAB */

	outb(quot & 0xff, uport->iobase + UART_DLL);	/* LS of divisor */
	outb(quot >> 8, uport->iobase + UART_DLM);	/* MS of divisor */
	outb(cval, uport->iobase + UART_LCR);	/* reset DLAB */

	if ((termios->c_cflag & CBAUD) == BOTHER) {
		quot = MXSER_BAUD_BASE % newspd;
		quot *= 8;
		if (quot % newspd > newspd / 2) {
			quot /= newspd;
			quot++;
		} else
			quot /= newspd;

		mxser_set_must_enum_value(uport->iobase, quot);
	} else {
		mxser_set_must_enum_value(uport->iobase, 0);
	}

	return 0;
}

static void mxser_handle_cts(struct mxser_port *info, u8 msr)
{
	struct uart_port *uport = &info->uport;
	bool cts = msr & UART_MSR_CTS;

	if (uport->hw_stopped) {
		if (cts) {
			uport->hw_stopped = false;

			if (!mxser_16550A_or_MUST(info))
				mxser_start_tx(uport);
			uart_write_wakeup(uport);
		}
		return;
	} else if (cts)
		return;

	uport->hw_stopped = true;
	if (!mxser_16550A_or_MUST(info))
		mxser_stop_tx(uport);
}

/*
 * This routine is called to set the UART divisor registers to match
 * the specified baud rate for a serial port.
 */
static void mxser_change_speed(struct mxser_port *info,
			       struct ktermios *termios,
			       const struct ktermios *old_termios)
{
	struct uart_port *uport = &info->uport;
	unsigned cflag, cval;

	cflag = termios->c_cflag;

	if (mxser_set_baud(info, termios)) {
		/* Use previous rate on a failure */
		if (old_termios) {
			speed_t baud = tty_termios_baud_rate(old_termios);
			tty_termios_encode_baud_rate(termios, baud, baud);
		}
	}

	/* byte size and parity */
	cval = UART_LCR_WLEN(tty_get_char_size(termios->c_cflag));

	if (cflag & CSTOPB)
		cval |= UART_LCR_STOP;
	if (cflag & PARENB)
		cval |= UART_LCR_PARITY;
	if (!(cflag & PARODD))
		cval |= UART_LCR_EPAR;
	if (cflag & CMSPAR)
		cval |= UART_LCR_SPAR;

	info->FCR = 0;
	if (info->board->must_hwid) {
		info->FCR |= UART_FCR_ENABLE_FIFO |
			MOXA_MUST_FCR_GDA_MODE_ENABLE;
		mxser_set_must_fifo_value(info);
	} else if (uport->type != PORT_8250 && uport->type != PORT_16450) {
		info->FCR |= UART_FCR_ENABLE_FIFO;
		switch (info->rx_high_water) {
		case 1:
			info->FCR |= UART_FCR_TRIGGER_1;
			break;
		case 4:
			info->FCR |= UART_FCR_TRIGGER_4;
			break;
		case 8:
			info->FCR |= UART_FCR_TRIGGER_8;
			break;
		default:
			info->FCR |= UART_FCR_TRIGGER_14;
			break;
		}
	}

	/* CTS flow control flag and modem status interrupts */
	info->IER &= ~UART_IER_MSI;
	info->MCR &= ~UART_MCR_AFE;
	//tty_port_set_cts_flow(&info->port, cflag & CRTSCTS);
	if (cflag & CRTSCTS) {
		info->IER |= UART_IER_MSI;
		if (mxser_16550A_or_MUST(info)) {
			info->MCR |= UART_MCR_AFE;
		} else {
			mxser_handle_cts(info, inb(uport->iobase + UART_MSR));
		}
	}
	outb(info->MCR, uport->iobase + UART_MCR);
	//tty_port_set_check_carrier(&info->port, ~cflag & CLOCAL);
	if (~cflag & CLOCAL)
		info->IER |= UART_IER_MSI;
	outb(info->IER, uport->iobase + UART_IER);

	/*
	 * Set up parity check flag
	 */
	uport->read_status_mask = UART_LSR_OE | UART_LSR_THRE | UART_LSR_DR;
	if (termios->c_iflag & INPCK)
		uport->read_status_mask |= UART_LSR_FE | UART_LSR_PE;
	if ((termios->c_iflag & BRKINT) || (termios->c_iflag & PARMRK))
		uport->read_status_mask |= UART_LSR_BI;

	uport->ignore_status_mask = 0;

	if (termios->c_iflag & IGNBRK) {
		uport->ignore_status_mask |= UART_LSR_BI;
		uport->read_status_mask |= UART_LSR_BI;
		/*
		 * If we're ignore parity and break indicators, ignore
		 * overruns too.  (For real raw support).
		 */
		if (termios->c_iflag & IGNPAR) {
			uport->ignore_status_mask |=
						UART_LSR_OE |
						UART_LSR_PE |
						UART_LSR_FE;
			uport->read_status_mask |=
						UART_LSR_OE |
						UART_LSR_PE |
						UART_LSR_FE;
		}
	}
	if (info->board->must_hwid) {
		mxser_set_must_xon1_value(uport->iobase, termios->c_cc[VSTART]);
		mxser_set_must_xoff1_value(uport->iobase, termios->c_cc[VSTOP]);
		mxser_must_set_rx_sw_flow_control(uport->iobase,
				termios->c_iflag & IXON);
		mxser_must_set_tx_sw_flow_control(uport->iobase,
				termios->c_iflag & IXOFF);
	}


	outb(info->FCR, uport->iobase + UART_FCR);
	outb(cval, uport->iobase + UART_LCR);
}

static u8 mxser_check_modem_status(struct mxser_port *port)
{
	struct uart_port *uport = &port->uport;
	u8 msr = inb(uport->iobase + UART_MSR);

	if (!(msr & UART_MSR_ANY_DELTA))
		return msr;

	/* update input line counters */
	if (msr & UART_MSR_TERI)
		uport->icount.rng++;
	if (msr & UART_MSR_DDSR)
		uport->icount.dsr++;
	if (msr & UART_MSR_DDCD)
		uart_handle_dcd_change(uport, msr & UART_MSR_DCD);
	if (msr & UART_MSR_DCTS)
		mxser_handle_cts(port, msr);
	wake_up_interruptible(&uport->state->port.delta_msr_wait);

	return msr;
}

static void mxser_disable_and_clear_FIFO(struct mxser_port *info)
{
	struct uart_port *uport = &info->uport;
	u8 fcr = UART_FCR_CLEAR_RCVR | UART_FCR_CLEAR_XMIT;

	if (info->board->must_hwid)
		fcr |= MOXA_MUST_FCR_GDA_MODE_ENABLE;

	outb(fcr, uport->iobase + UART_FCR);
}

static int mxser_startup(struct uart_port *uport)
{
	struct mxser_port *info = to_mport(uport);
	unsigned long flags;

	uart_port_lock_irqsave(uport, &flags);

	if (!uport->type) {
		uart_port_unlock_irqrestore(uport, flags);
		return -EINVAL;
	}

	/*
	 * Clear the FIFO buffers and disable them
	 * (they will be reenabled in mxser_change_speed())
	 */
	mxser_disable_and_clear_FIFO(info);

	/*
	 * At this point there's no way the LSR could still be 0xFF;
	 * if it is, then bail out, because there's likely no UART
	 * here.
	 */
	if (inb(uport->iobase + UART_LSR) == 0xff) {
		uart_port_unlock_irqrestore(uport, flags);
		return -ENODEV;
	}

	/*
	 * Clear the interrupt registers.
	 */
	(void) inb(uport->iobase + UART_LSR);
	(void) inb(uport->iobase + UART_RX);
	(void) inb(uport->iobase + UART_IIR);
	(void) inb(uport->iobase + UART_MSR);

	/*
	 * Now, initialize the UART
	 */
	outb(UART_LCR_WLEN8, uport->iobase + UART_LCR);	/* reset DLAB */
	info->MCR = UART_MCR_DTR | UART_MCR_RTS;
	outb(info->MCR, uport->iobase + UART_MCR);

	/*
	 * Finally, enable interrupts
	 */
	info->IER = UART_IER_MSI | UART_IER_RLSI | UART_IER_RDI;

	if (info->board->must_hwid)
		info->IER |= MOXA_MUST_IER_EGDAI;
	outb(info->IER, uport->iobase + UART_IER);	/* enable interrupts */

	/*
	 * And clear the interrupt registers again for luck.
	 */
	(void) inb(uport->iobase + UART_LSR);
	(void) inb(uport->iobase + UART_RX);
	(void) inb(uport->iobase + UART_IIR);
	(void) inb(uport->iobase + UART_MSR);

	uart_port_unlock_irqrestore(uport, flags);

	return 0;
}

/*
 * To stop accepting input, we disable the receive line status interrupts, and
 * tell the interrupt driver to stop checking the data ready bit in the line
 * status register.
 */
static void mxser_stop_rx(struct uart_port *uport)
{
	struct mxser_port *info = to_mport(uport);

	info->IER &= ~UART_IER_RLSI;
	if (info->board->must_hwid)
		info->IER &= ~MOXA_MUST_RECV_ISR;

	outb(info->IER, uport->iobase + UART_IER);
}

/*
 * This routine will shutdown a serial port
 */
static void mxser_shutdown(struct uart_port *uport)
{
	struct mxser_port *info = to_mport(uport);
	unsigned long flags;

	uart_port_lock_irqsave(uport, &flags);

	info->IER = 0;
	outb(0x00, uport->iobase + UART_IER);

	/* clear Rx/Tx FIFO's */
	mxser_disable_and_clear_FIFO(info);

	/* read data port to reset things */
	(void) inb(uport->iobase + UART_RX);


	if (info->board->must_hwid)
		mxser_must_no_sw_flow_control(uport->iobase);

	uart_port_unlock_irqrestore(uport, flags);
}

static void mxser_flush_buffer(struct uart_port *uport)
{
	struct mxser_port *info = to_mport(uport);

	outb(info->FCR | UART_FCR_CLEAR_RCVR | UART_FCR_CLEAR_XMIT,
		uport->iobase + UART_FCR);
}

static int mxser_verify_port(struct uart_port *uport, struct serial_struct *ss)
{
	if (ss->irq != uport->irq)
		return -EINVAL;
	if (ss->port != uport->iobase)
		return -EINVAL;

	return 0;
}

#ifdef OLD
static int mxser_set_serial_info(struct tty_struct *tty,
		struct serial_struct *ss)
{
	struct mxser_port *info = tty->driver_data;
	struct uart_port *uport = &info->uport;
	struct tty_port *port = &info->port;
	speed_t baud;
	unsigned long sl_flags;
	unsigned int old_speed, close_delay, closing_wait;
	int retval = 0;

	if (tty_io_error(tty))
		return -EIO;

	mutex_lock(&port->mutex);

	if (ss->irq != uport->irq || ss->port != uport->iobase) {
		mutex_unlock(&port->mutex);
		return -EINVAL;
	}

	old_speed = port->flags & ASYNC_SPD_MASK;

	close_delay = msecs_to_jiffies(ss->close_delay * 10);
	closing_wait = ss->closing_wait;
	if (closing_wait != ASYNC_CLOSING_WAIT_NONE)
		closing_wait = msecs_to_jiffies(closing_wait * 10);

	if (!capable(CAP_SYS_ADMIN)) {
		if ((ss->baud_base != MXSER_BAUD_BASE) ||
				(close_delay != port->close_delay) ||
				(closing_wait != port->closing_wait) ||
				((ss->flags & ~ASYNC_USR_MASK) != (port->flags & ~ASYNC_USR_MASK))) {
			mutex_unlock(&port->mutex);
			return -EPERM;
		}
		port->flags = (port->flags & ~ASYNC_USR_MASK) |
				(ss->flags & ASYNC_USR_MASK);
	} else {
		/*
		 * OK, past this point, all the error checking has been done.
		 * At this point, we start making changes.....
		 */
		port->flags = ((port->flags & ~ASYNC_FLAGS) |
				(ss->flags & ASYNC_FLAGS));
		port->close_delay = close_delay;
		port->closing_wait = closing_wait;
		if ((port->flags & ASYNC_SPD_MASK) == ASYNC_SPD_CUST &&
				(ss->baud_base != MXSER_BAUD_BASE ||
				ss->custom_divisor !=
				MXSER_CUSTOM_DIVISOR)) {
			if (ss->custom_divisor == 0) {
				mutex_unlock(&port->mutex);
				return -EINVAL;
			}
			baud = ss->baud_base / ss->custom_divisor;
			tty_encode_baud_rate(tty, baud, baud);
		}

		uport->type = ss->type;

		mxser_process_txrx_fifo(info); // TODO
	}

	if (tty_port_initialized(port)) {
		if (old_speed != (port->flags & ASYNC_SPD_MASK)) {
			spin_lock_irqsave(&uport->lock, sl_flags);
			mxser_change_speed(info, &tty->termios, NULL);
			spin_unlock_irqrestore(&uport->lock, sl_flags);
		}
	} else {
		retval = mxser_activate(port, tty);
		if (retval == 0)
			tty_port_set_initialized(port, true);
	}
	mutex_unlock(&port->mutex);
	return retval;
}
#endif

static unsigned int mxser_get_mctrl(struct uart_port *uport)
{
	struct mxser_port *info = to_mport(uport);
	u8 msr, control;

	control = info->MCR;
	msr = mxser_check_modem_status(info);

	return ((control & UART_MCR_RTS) ? TIOCM_RTS : 0) |
		    ((control & UART_MCR_DTR) ? TIOCM_DTR : 0) |
		    ((msr & UART_MSR_DCD) ? TIOCM_CAR : 0) |
		    ((msr & UART_MSR_RI) ? TIOCM_RNG : 0) |
		    ((msr & UART_MSR_DSR) ? TIOCM_DSR : 0) |
		    ((msr & UART_MSR_CTS) ? TIOCM_CTS : 0);
}

static void mxser_set_mctrl(struct uart_port *uport, unsigned int mctrl)
{
	struct mxser_port *info = to_mport(uport);

	if (mctrl & TIOCM_RTS)
		info->MCR |= UART_MCR_RTS;
	else
		info->MCR &= ~UART_MCR_RTS;

	if (mctrl & TIOCM_DTR)
		info->MCR |= UART_MCR_DTR;
	else
		info->MCR &= ~UART_MCR_DTR;

	outb(info->MCR, uport->iobase + UART_MCR);
}
#ifdef OLD
/* We should likely switch to TIOCGRS485/TIOCSRS485. */
static int mxser_ioctl_op_mode(struct mxser_port *port, int index, bool set,
		int __user *u_opmode)
{
	struct uart_port *uport = &port->uport;
	int opmode, p = index % 4;
	int shiftbit = p * 2;
	u8 val;

	if (port->board->must_hwid != MOXA_MUST_MU860_HWID)
		return -EFAULT;

	if (set) {
		if (get_user(opmode, u_opmode))
			return -EFAULT;

		if (opmode & ~OP_MODE_MASK)
			return -EINVAL;

		spin_lock_irq(&uport->lock);
		val = inb(port->opmode_ioaddr);
		val &= ~(OP_MODE_MASK << shiftbit);
		val |= (opmode << shiftbit);
		outb(val, port->opmode_ioaddr);
		spin_unlock_irq(&uport->lock);

		return 0;
	}

	spin_lock_irq(&uport->lock);
	opmode = inb(port->opmode_ioaddr) >> shiftbit;
	spin_unlock_irq(&uport->lock);

	return put_user(opmode & OP_MODE_MASK, u_opmode);
}

static int mxser_ioctl(struct tty_struct *tty,
		unsigned int cmd, unsigned long arg)
{
	struct mxser_port *info = tty->driver_data;
	struct uart_port *uport = &info->uport;
	struct uart_icount cnow;
	unsigned long flags;
	void __user *argp = (void __user *)arg;

	if (cmd == MOXA_SET_OP_MODE || cmd == MOXA_GET_OP_MODE)
		return mxser_ioctl_op_mode(info, tty->index,
				cmd == MOXA_SET_OP_MODE, argp);

	return -EIO;
}

/*
 * This routine is called by the upper-layer tty layer to signal that
 * incoming characters should be throttled.
 */
static void mxser_throttle(struct tty_struct *tty)
{
	struct mxser_port *info = tty->driver_data;
	struct uart_port *uport = &info->uport;

	if (I_IXOFF(tty)) {
		if (info->board->must_hwid) {
			info->IER &= ~MOXA_MUST_RECV_ISR;
			outb(info->IER, uport->iobase + UART_IER);
		} else {
			uport->x_char = STOP_CHAR(tty);
			outb(0, uport->iobase + UART_IER);
			info->IER |= UART_IER_THRI;
			outb(info->IER, uport->iobase + UART_IER);
		}
	}

	if (C_CRTSCTS(tty)) {
		info->MCR &= ~UART_MCR_RTS;
		outb(info->MCR, uport->iobase + UART_MCR);
	}
}

static void mxser_unthrottle(struct tty_struct *tty)
{
	struct mxser_port *info = tty->driver_data;
	struct uart_port *uport = &info->uport;

	/* startrx */
	if (I_IXOFF(tty)) {
		if (uport->x_char)
			uport->x_char = 0;
		else {
			if (info->board->must_hwid) {
				info->IER |= MOXA_MUST_RECV_ISR;
				outb(info->IER, uport->iobase + UART_IER);
			} else {
				uport->x_char = START_CHAR(tty);
				outb(0, uport->iobase + UART_IER);
				info->IER |= UART_IER_THRI;
				outb(info->IER, uport->iobase + UART_IER);
			}
		}
	}

	if (C_CRTSCTS(tty)) {
		info->MCR |= UART_MCR_RTS;
		outb(info->MCR, uport->iobase + UART_MCR);
	}
}

/*
 * mxser_stop() and mxser_start()
 *
 * This routines are called before setting or resetting tty->flow.stopped.
 * They enable or disable transmitter interrupts, as necessary.
 */
static void mxser_stop(struct tty_struct *tty)
{
	struct mxser_port *info = tty->driver_data;
	struct uart_port *uport = &info->uport;
	unsigned long flags;

	spin_lock_irqsave(&uport->lock, flags);
	if (info->IER & UART_IER_THRI)
		mxser_stop_tx(uport);
	spin_unlock_irqrestore(&uport->lock, flags);
}

static void mxser_start(struct tty_struct *tty)
{
	struct mxser_port *info = tty->driver_data;
	struct uart_port *uport = &info->uport;
	unsigned long flags;

	spin_lock_irqsave(&uport->lock, flags);
	if (!kfifo_is_empty(&info->port.xmit_fifo))
		__mxser_start_tx(info);
	spin_unlock_irqrestore(&uport->lock, flags);
}
#endif
static void mxser_set_termios(struct uart_port *uport, struct ktermios *termios,
			      const struct ktermios *old_termios)
{
	struct mxser_port *info = to_mport(uport);
	unsigned long flags;

	uart_port_lock_irqsave(uport, &flags);
	mxser_change_speed(info, termios, old_termios);
	uart_port_unlock_irqrestore(uport, flags);
}

static const char *mxser_type(struct uart_port *port)
{
	return port->type == PORT_16550A ? "16550A" : NULL;
}

static unsigned int mxser_tx_empty(struct uart_port *uport)
{
	if (inb(uport->iobase + UART_LSR) & UART_LSR_TEMT)
		return TIOCSER_TEMT;

	return 0;
}

/*
 * mxser_break_ctl() --- routine which turns the break handling on or off
 */
static void mxser_break_ctl(struct uart_port *uport, int break_state)
{
	unsigned long flags;
	u8 lcr;

	spin_lock_irqsave(&uport->lock, flags);
	lcr = inb(uport->iobase + UART_LCR);
	if (break_state == -1)
		lcr |= UART_LCR_SBC;
	else
		lcr &= ~UART_LCR_SBC;

	outb(lcr, uport->iobase + UART_LCR);
	spin_unlock_irqrestore(&uport->lock, flags);
}

static bool mxser_receive_chars_new(struct mxser_port *port, u8 status)
{
	struct uart_port *uport = &port->uport;
	enum mxser_must_hwid hwid = port->board->must_hwid;
	u8 gdl;

	if (hwid == MOXA_OTHER_UART)
		return false;
	if (status & (UART_LSR_BRK_ERROR_BITS | MOXA_MUST_LSR_RERR))
		return false;

	gdl = inb(uport->iobase + MOXA_MUST_GDL_REGISTER);
	if (hwid == MOXA_MUST_MU150_HWID)
		gdl &= MOXA_MUST_GDL_MASK;

	while (gdl--) {
		u8 ch = inb(uport->iobase + UART_RX);
                if (!uart_prepare_sysrq_char(uport, ch))
                        uart_insert_char(uport, 0, 0, ch, 0);
	}

	return true;
}

static u8 mxser_receive_chars_old(struct mxser_port *port, u8 status)
{
	struct uart_port *uport = &port->uport;
	enum mxser_must_hwid hwid = port->board->must_hwid;
	int ignored = 0;
	int max = 256;
	u8 ch;

	do {
		if (max-- < 0)
			break;

		ch = inb(uport->iobase + UART_RX);
		if (hwid && (status & UART_LSR_OE))
			outb(port->FCR | UART_FCR_CLEAR_RCVR,
					uport->iobase + UART_FCR);
		status &= uport->read_status_mask;
		if (status & uport->ignore_status_mask) {
			if (++ignored > 100)
				break;
		} else {
			u8 flag = 0;
			if (status & UART_LSR_BRK_ERROR_BITS) {
				if (status & UART_LSR_BI) {
					flag = TTY_BREAK;
					uport->icount.brk++;
					if (uart_handle_break(uport))
						continue;
				} else if (status & UART_LSR_PE) {
					flag = TTY_PARITY;
					uport->icount.parity++;
				} else if (status & UART_LSR_FE) {
					flag = TTY_FRAME;
					uport->icount.frame++;
				} else if (status & UART_LSR_OE) {
					flag = TTY_OVERRUN;
					uport->icount.overrun++;
				}
			}
			if (!uart_prepare_sysrq_char(uport, ch))
				uart_insert_char(uport, status, UART_LSR_OE, ch,
						 flag);
		}

		if (hwid)
			break;

		status = inb(uport->iobase + UART_LSR);
	} while (status & UART_LSR_DR);

	return status;
}

static u8 mxser_receive_chars(struct mxser_port *port, u8 status)
{
	if (!mxser_receive_chars_new(port, status))
		status = mxser_receive_chars_old(port, status);

	tty_flip_buffer_push(&port->uport.state->port);

	return status;
}

static void mxser_transmit_chars(struct mxser_port *port)
{
	struct uart_port *uport = &port->uport;
	struct tty_port *tport = &uport->state->port;
	int count;

	if (uport->x_char) {
		outb(uport->x_char, uport->iobase + UART_TX);
		uport->x_char = 0;
		uport->icount.tx++;
		return;
	}

	if (kfifo_is_empty(&tport->xmit_fifo) ||
	    (tport->tty && tport->tty->flow.stopped) ||
	    (uport->hw_stopped && !mxser_16550A_or_MUST(port))) {
		mxser_stop_tx(uport);
		return;
	}

	count = uport->fifosize;
	do {
		u8 c;

		if (!kfifo_get(&tport->xmit_fifo, &c))
			break;

		outb(c, uport->iobase + UART_TX);
		uport->icount.tx++;
	} while (--count > 0);

	if (kfifo_len(&tport->xmit_fifo) < WAKEUP_CHARS)
		uart_write_wakeup(uport);

	if (kfifo_is_empty(&tport->xmit_fifo))
		mxser_stop_tx(uport);
}

static bool mxser_port_isr(struct mxser_port *port)
{
	struct uart_port *uport = &port->uport;
	u8 iir, status;

	iir = inb(uport->iobase + UART_IIR);
	if (iir & UART_IIR_NO_INT)
		return true;

	iir &= MOXA_MUST_IIR_MASK;

	status = inb(uport->iobase + UART_LSR);

	if (port->board->must_hwid) {
		if (iir == MOXA_MUST_IIR_GDA ||
		    iir == MOXA_MUST_IIR_RDA ||
		    iir == MOXA_MUST_IIR_RTO ||
		    iir == MOXA_MUST_IIR_LSR)
			status = mxser_receive_chars(port, status);
	} else {
		status &= uport->read_status_mask;
		if (status & UART_LSR_DR)
			status = mxser_receive_chars(port, status);
	}

	mxser_check_modem_status(port);

	if (port->board->must_hwid) {
		if (iir == 0x02 && (status & UART_LSR_THRE))
			mxser_transmit_chars(port);
	} else {
		if (status & UART_LSR_THRE)
			mxser_transmit_chars(port);
	}

	return false;
}
/*
 * This is the serial driver's generic interrupt routine
 */
static irqreturn_t mxser_interrupt(int irq, void *dev_id)
{
	struct mxser_board *brd = dev_id;
	struct mxser_port *port;
	struct uart_port *uport;
	unsigned int int_cnt, pass_counter = 0;
	unsigned int i, max = brd->nports;
	int handled = IRQ_NONE;
	u8 irqbits, bits, mask = BIT(max) - 1;

	while (pass_counter++ < MXSER_ISR_PASS_LIMIT) {
		irqbits = inb(brd->vector) & mask;
		if (irqbits == mask)
			break;

		handled = IRQ_HANDLED;
		for (i = 0, bits = 1; i < max; i++, irqbits |= bits, bits <<= 1) {
			if (irqbits == mask)
				break;
			if (bits & irqbits)
				continue;
			port = &brd->ports[i];
			uport = &port->uport;

			int_cnt = 0;
			uart_port_lock(uport);
			do {
				if (mxser_port_isr(port))
					break;
			} while (int_cnt++ < MXSER_ISR_PASS_LIMIT);
			uart_port_unlock(uport);
		}
	}

	return handled;
}

static struct uart_driver mxser_uart_driver = {
	.owner		= THIS_MODULE,
	.dev_name	= "ttyMI",
	.nr		= MXSER_PORTS,
};

static void mxser_config_port(struct uart_port *port, int flags)
{
	if (flags & UART_CONFIG_TYPE)
                port->type = PORT_16550A;
}

static const struct uart_ops mxser_ops = {
	.tx_empty       = mxser_tx_empty,
	.get_mctrl      = mxser_get_mctrl,
	.set_mctrl      = mxser_set_mctrl,
	.stop_tx        = mxser_stop_tx,
	.start_tx       = mxser_start_tx,
	.stop_rx        = mxser_stop_rx,
	.break_ctl      = mxser_break_ctl,
	.startup        = mxser_startup,
	.shutdown       = mxser_shutdown,
	.flush_buffer	= mxser_flush_buffer,
	.set_termios    = mxser_set_termios,
	.type           = mxser_type,
	.config_port    = mxser_config_port,
	.verify_port    = mxser_verify_port,
};


/*
 * The MOXA Smartio/Industio serial driver boot-time initialization code!
 */

static unsigned short mxser_get_nports(struct pci_dev *pdev)
{
	if (pdev->device == PCI_DEVICE_ID_MOXA_RC7000)
		return 8;

	return FIELD_GET(0x00F0, pdev->device);
}

static void mxser_initbrd(struct pci_dev *pdev, struct mxser_board *brd,
			  bool high_baud)
{
	unsigned long ioaddress = pci_resource_start(pdev, 2);
	unsigned int i;
	bool is_mu860;

	brd->nports = mxser_get_nports(pdev);
	brd->vector = pci_resource_start(pdev, 3);

	brd->must_hwid = mxser_must_get_hwid(ioaddress);
	is_mu860 = brd->must_hwid == MOXA_MUST_MU860_HWID;

	for (i = 0; i < UART_INFO_NUM; i++) {
		if (Gpci_uart_info[i].type == brd->must_hwid) {
			brd->max_baud = Gpci_uart_info[i].max_baud;

			/* exception....CP-102 */
			if (high_baud)
				brd->max_baud = 921600;
			break;
		}
	}

	if (is_mu860) {
		/* set to RS232 mode by default */
		outb(0, brd->vector + 4);
		outb(0, brd->vector + 0x0c);
	}

	for (i = 0; i < brd->nports; i++) {
		struct mxser_port *info = &brd->ports[i];
		struct uart_port *uport = &info->uport;
		if (is_mu860) {
			if (i < 4)
				info->opmode_ioaddr = brd->vector + 4;
			else
				info->opmode_ioaddr = brd->vector + 0x0c;
		}
		info->board = brd;
		uport->iobase = ioaddress + 8 * i;

		/* Enhance mode enabled here */
		if (brd->must_hwid != MOXA_OTHER_UART)
			mxser_must_set_enhance_mode(uport->iobase, true);

		uport->dev = &pdev->dev;
		uport->iotype = UPIO_PORT;
		uport->irq = pdev->irq;
		uport->ops = &mxser_ops;
		uport->flags = UPF_BOOT_AUTOCONF;
		uport->line = i;

		mxser_process_txrx_fifo(info);

		/* before set INT ISR, disable all int */
		outb(inb(uport->iobase + UART_IER) & 0xf0,
			uport->iobase + UART_IER);
	}
}

static int mxser_probe(struct pci_dev *pdev,
		const struct pci_device_id *ent)
{
	struct mxser_board *brd;
	unsigned int i;
	unsigned short nports = mxser_get_nports(pdev);
	int retval = -EINVAL;

	i = find_first_zero_bit(mxser_boards, MXSER_BOARDS);
	if (i >= MXSER_BOARDS) {
		dev_err(&pdev->dev, "too many boards found (maximum %d), board "
				"not configured\n", MXSER_BOARDS);
		goto err;
	}

	brd = devm_kzalloc(&pdev->dev, struct_size(brd, ports, nports),
			GFP_KERNEL);
	if (!brd)
		goto err;

	brd->idx = i;
	__set_bit(brd->idx, mxser_boards);

	retval = pcim_enable_device(pdev);
	if (retval) {
		dev_err(&pdev->dev, "PCI enable failed\n");
		goto err_zero;
	}

	/* io address */
	retval = pci_request_region(pdev, 2, "mxser(IO)");
	if (retval)
		goto err_zero;

	/* vector */
	retval = pci_request_region(pdev, 3, "mxser(vector)");
	if (retval)
		goto err_zero;

	mxser_initbrd(pdev, brd, ent->driver_data & MXSER_HIGHBAUD);

	retval = devm_request_irq(&pdev->dev, pdev->irq, mxser_interrupt,
			IRQF_SHARED, "mxser", brd);
	if (retval) {
		dev_err(&pdev->dev, "request irq failed");
		goto err_zero;
	}

	for (i = 0; i < nports; i++) {
		retval = uart_add_one_port(&mxser_uart_driver,
				&brd->ports[i].uport);
		if (retval) {
			for (; i > 0; i--)
				uart_remove_one_port(&mxser_uart_driver,
						&brd->ports[i - 1].uport);
			goto err_zero;
		}
	}

	pci_set_drvdata(pdev, brd);

	return 0;
err_zero:
	__clear_bit(brd->idx, mxser_boards);
err:
	return retval;
}

static void mxser_remove(struct pci_dev *pdev)
{
	struct mxser_board *brd = pci_get_drvdata(pdev);
	unsigned int i;

	for (i = 0; i < brd->nports; i++)
		uart_remove_one_port(&mxser_uart_driver, &brd->ports[i].uport);

	__clear_bit(brd->idx, mxser_boards);
}

static struct pci_driver mxser_driver = {
	.name = "mxser",
	.id_table = mxser_pcibrds,
	.probe = mxser_probe,
	.remove = mxser_remove
};

static int __init mxser_module_init(void)
{
	int retval;

	mxser_uart_driver.major = ttymajor;

	retval = uart_register_driver(&mxser_uart_driver);
	if (retval)
		return retval;

	retval = pci_register_driver(&mxser_driver);
	if (retval) {
		printk(KERN_ERR "mxser: can't register pci driver\n");
		goto err_unr;
	}

	return 0;
err_unr:
	uart_unregister_driver(&mxser_uart_driver);
	return retval;
}

static void __exit mxser_module_exit(void)
{
	pci_unregister_driver(&mxser_driver);
	uart_unregister_driver(&mxser_uart_driver);
}

module_init(mxser_module_init);
module_exit(mxser_module_exit);
