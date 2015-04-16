#include <linux/tty.h>
#include <linux/tty_driver.h>

#include "../../common.c"

struct console *console_drivers;

#include <linux/keyboard.h>
ushort *key_maps[MAX_NR_KEYMAPS];
unsigned int accent_table_size;
char *func_table[MAX_NR_FUNC];
char func_buf[] = { };
char *funcbufptr = func_buf;
int funcbufsize = sizeof(func_buf);
unsigned int keymap_count = 7;
int funcbufleft = 0;

struct device *class_find_device(struct class *class, struct device *start,
		const void *data, int (*match)(struct device *, const void *))
{
	return NULL;
}

/* ================== */
static struct tty_driver *tty_drv;

static struct tty_struct *alloc_tty(const char *name)
{
	struct tty_port *port;
	struct tty_struct *tty;

	my_make_symbolic = 1;
	my_make_symbolic_str = name;
	tty = alloc_tty_struct(tty_drv, 0);

	port = malloc(sizeof(*port));
	tty_port_init(port);

	tty->port = port;
	tty->ops = tty_drv->ops;

	return tty;
}

static int write(struct tty_struct * tty,
		      const unsigned char *buf, int count)

{
	return count;
}

static int break_ctl(struct tty_struct *tty, int state)
{
	return klee_int("break_ctl");
}

static int chars_in_buffer(struct tty_struct *tty)
{
	return klee_int("chars_in_buffer");
}

#define TTY_LINES 1

static int prepare_driver(void)
{
	static struct tty_operations tty_ops = {
		.write = write,
		.break_ctl = break_ctl,
		.chars_in_buffer = chars_in_buffer,
	};

	my_make_symbolic = 1;
	my_make_symbolic_str = "tty_driver";
	tty_drv = tty_alloc_driver(TTY_LINES, 0);
	if (IS_ERR(tty_drv)) {
		klee_warning("no driver\n");
		printf("err=%d\n", PTR_ERR(tty_drv));
		return -ENOMEM;
	}
	tty_set_operations(tty_drv, &tty_ops);

	tty_drv->name = "ktd";
	tty_drv->name_base = 0;
	if (tty_drv->type == TTY_DRIVER_TYPE_PTY)
		return -EINVAL;
	/* fooling :) */
	tty_drv->flags |= TTY_DRIVER_INSTALLED;

	return 0;
}

static int prepare(void)
{
	int ret;

	n_tty_init();

	ret = prepare_driver();
	if (ret)
		return ret;

	return 0;
}

extern ssize_t tty_write(struct file *file, const char __user *buf,
		size_t count, loff_t *ppos);

int main()
{
	struct tty_file_private tfp;
	struct file_operations fop;
	struct inode inode;
	struct file file = {
		.f_op = &fop,
		.f_inode = &inode,
		.private_data = &tfp,
	};
	struct tty_struct *tty;
	unsigned int cmd;
	unsigned long arg;
	loff_t pos = 0;

	if (prepare())
		return 0;

	klee_make_symbolic(&cmd, sizeof(cmd), "cmd");
	if (0 && klee_int("pointer_arg")) {
#define ARG_SIZE 1024
		void *ptr = malloc(ARG_SIZE);
		klee_make_symbolic(ptr, ARG_SIZE, "arg");
		arg = (unsigned long)ptr;
	} else
		klee_make_symbolic(&arg, sizeof(arg), "arg");

	tty = alloc_tty("tty");
	tfp.tty = tty;

	if (1 || klee_int("curr->signal->tty==tty"))
		curr_sig.tty = tty;

	my_make_symbolic = 1;
	my_make_symbolic_str = "n_tty_data";
	tty_ldisc_setup(tty, NULL);

//	klee_warning("doing ioctl:\n");
//	tty_ioctl(&file, cmd, arg);
#define BUF_SIZE 256
	void *buf = malloc(BUF_SIZE);
	klee_make_symbolic(buf, BUF_SIZE, "buf");
	void *flg = malloc(BUF_SIZE);
	klee_make_symbolic(flg, BUF_SIZE, "flg");

	tty->termios.c_oflag = klee_int("oflag");
	tty->termios.c_iflag = klee_int("iflag");
	tty->termios.c_cflag = klee_int("cflag");
	tty->termios.c_lflag = klee_int("lflag");
	//tty_write(&file, NULL, BUF_SIZE, &pos);
	tty->ldisc->ops->set_termios(tty, NULL);
	tty_ldisc_receive_buf(tty->ldisc, buf, flg, BUF_SIZE);
	return 0;
}
