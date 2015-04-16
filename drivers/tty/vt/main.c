#include <linux/tty.h>
#include <linux/console.h>
#include <linux/kd.h>
#include <linux/console_struct.h>
#include <linux/kernel.h>

#include "../../../common.c"

#include <linux/keyboard.h>
ushort *key_maps[MAX_NR_KEYMAPS];
unsigned int accent_table_size;

/**************************************/

void console_lock(void)
{
}

void console_unlock(void)
{
}

void register_console(struct console *newcon)
{
}

void __sched console_conditional_schedule(void)
{
}

extern int __init con_init(void);
extern int do_con_write(struct tty_struct *tty, const unsigned char *buf,
		int count);
extern void do_con_trol(struct tty_struct *tty, struct vc_data *vc, int c);

#define BUF_SIZE 1024

static void con_cursor(struct vc_data *c, int mode)
{
}

static const char *con_startup(void)
{
    return "my_con";
}

static void my_con_init(struct vc_data *vc, int init)
{
    vc->vc_can_do_color = klee_int("vc_can_do_color");
    if (init) {
	vc->vc_cols = 80;
	vc->vc_rows = 25;
    } else
	vc_resize(vc, 80, 25);
}

static int set_palette(struct vc_data *vc, const unsigned char *table)
{
	return 0;
}

static void con_putcs(struct vc_data *vc, const unsigned short *s,
			int count, int ypos, int xpos)
{
}

static int con_switch(struct vc_data *c)
{
	return 1;
}

static int con_scroll(struct vc_data *c, int t, int b, int dir, int lines)
{
	return 0;
}

static void con_clear(struct vc_data *c, int y, int x, int height, int width)
{
}

int main(void)
{
	struct consw sw = {
		.con_init = my_con_init,
		.con_cursor = con_cursor,
		.con_startup = con_startup,
		.con_set_palette = set_palette,
		.con_putcs = con_putcs,
		.con_switch = con_switch,
		.con_scroll = con_scroll,
		.con_clear = con_clear,
	};
	struct tty_struct tty = {
	};
	struct vc_data *master_display_fg;
	struct vc_data vc = {
		.vc_cols = 80,
		.vc_rows = 25,
		.vc_sw = &sw,
		.vc_display_fg = &master_display_fg,
	};
	unsigned a, b;
	char *buf;

	master_display_fg = &vc;
	vc.vc_size_row = vc.vc_cols << 1;
	vc.vc_screenbuf_size = vc.vc_rows * vc.vc_size_row;
	vc.vc_screenbuf = malloc(vc.vc_screenbuf_size);
	vc.vc_origin = (unsigned long)vc.vc_screenbuf;
	vc.vc_visible_origin = vc.vc_origin;
	vc.vc_scr_end = vc.vc_origin + vc.vc_screenbuf_size;
	vc.vc_pos = vc.vc_origin + vc.vc_size_row * vc.vc_y + 2 * vc.vc_x;

	current_task = malloc(sizeof(struct task_struct));
	memset(current_task, 0, sizeof(struct task_struct));
	current_task->pid = 1; /* let it be init :) */

	conswitchp = &sw;
/*	con_init();
	tty.driver_data = vc_cons[0].d;
	tty.port = &vc_cons[0].d->port;*/
	tty.port = &vc.port;
	tty_port_init(tty.port);

	//for (a = 20; a < 21; a++) 
	a = 10;
	{
		buf = malloc(a);
		klee_make_symbolic(buf, a, "buf");
		klee_assume(buf[0] == 27);
		klee_assume(buf[1] == '[');
		for (b = 2; b < a; b++) {
			if (buf[b] != 0 && (buf[b] < '0' || buf[b] >= 127))
				return 0;
/*			klee_assume(buf[b] == 0 ||
					(buf[b] >= '0' && buf[b] < 127));*/
		}
		int had_zero = 0;
		for (b = 2; b < a; b++) {
			if (buf[b] != 0 && had_zero)
				return 0;
			if (buf[b] == 0)
				had_zero = 1;
		}
//		do_con_write(&tty, buf, a);

		for (b = 0; b < a; b++)
			do_con_trol(&tty, &vc, buf[b]);

		free(buf);
	}

	return 0;
}
