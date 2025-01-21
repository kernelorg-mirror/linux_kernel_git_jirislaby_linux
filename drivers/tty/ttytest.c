// SPDX-License-Identifier: GPL-2.0
/*
 * Copyright (C) 2025 Jiri Slaby
 */

#include <linux/list.h>
#include <linux/module.h>
#include <linux/tty.h>
#include <linux/tty_flip.h>

struct ttytest_port {
	struct tty_port port;
	struct delayed_work rx_work;
	struct list_head list;
	unsigned int index;
};

static LIST_HEAD(ttytest_ports);
static struct tty_driver *ttytest_driver;

static int ttytest_open(struct tty_struct *tty, struct file *filp)
{
	return tty_port_open(tty->port, tty, filp);
}

static void ttytest_close(struct tty_struct *tty, struct file *filp)
{
	tty_port_close(tty->port, tty, filp);
}

static void ttytest_hangup(struct tty_struct *tty)
{
	tty_port_hangup(tty->port);
}

static ssize_t ttytest_write(struct tty_struct *tty, const u8 *buf,
			     size_t count)
{
	print_hex_dump(KERN_DEBUG, "W: ", DUMP_PREFIX_NONE, 16, 8, buf,
		       min(count, 1024), true);
	return count;
}

static unsigned int ttytest_write_room(struct tty_struct *tty)
{
	return UINT_MAX;
}

static void ttytest_rx_work(struct work_struct *work)
{
	struct ttytest_port *tport = container_of(work, struct ttytest_port,
						  rx_work.work);
	size_t len;
	u8 buf[16];

	len = scnprintf(buf, sizeof(buf), "HERE %u\n", tport->index);
	tty_insert_flip_string(&tport->port, buf, len);
	tty_flip_buffer_push(&tport->port);

	schedule_delayed_work(&tport->rx_work, HZ);
}

static const struct tty_operations ttytest_ops = {
	.open = ttytest_open,
	.close = ttytest_close,
	.hangup = ttytest_hangup,
	.write = ttytest_write,
	.write_room = ttytest_write_room,
};

static inline struct ttytest_port *of_port(struct tty_port *port)
{
	return container_of(port, struct ttytest_port, port);
}

static int ttytest_port_activate(struct tty_port *port, struct tty_struct *tty)
{
	struct ttytest_port *tport = of_port(port);

	schedule_delayed_work(&tport->rx_work, HZ);

	return 0;
}

static void ttytest_port_shutdown(struct tty_port *port)
{
	struct ttytest_port *tport = of_port(port);

	cancel_delayed_work(&tport->rx_work);
}

static void ttytest_port_destruct(struct tty_port *port)
{
	struct ttytest_port *tport = of_port(port);

	pr_info("dealloc %px\n", tport);
	kfree(tport);
}

static const struct tty_port_operations ttytest_port_ops = {
	.activate = ttytest_port_activate,
	.shutdown = ttytest_port_shutdown,
	.destruct = ttytest_port_destruct,
};

static struct ttytest_port *ttytest_add_one(unsigned int index)
{
	struct ttytest_port *tport;
	struct device *ttydev;

	tport = kzalloc(sizeof(*tport), GFP_KERNEL);
	if (!tport)
		return ERR_PTR(-ENOMEM);
	pr_info("alloc %px\n", tport);

	tty_port_init(&tport->port);
	tport->port.ops = &ttytest_port_ops;
	tport->index = index;

	INIT_DELAYED_WORK(&tport->rx_work, ttytest_rx_work);

	ttydev = tty_port_register_device(&tport->port, ttytest_driver, index,
					  NULL);
	if (IS_ERR(ttydev)) {
		tty_port_put(&tport->port);
		return ERR_CAST(ttydev);
	}

	list_add_tail(&tport->list, &ttytest_ports);

	return tport;
}

static void ttytest_remove_one(struct ttytest_port *tport)
{
	tty_port_unregister_device(&tport->port, ttytest_driver, tport->index);
	list_del(&tport->list);
	tty_port_put(&tport->port);
}

static void ttytest_remove_all(void)
{
	struct ttytest_port *tport, *n;

	list_for_each_entry_safe(tport, n, &ttytest_ports, list) {
		ttytest_remove_one(tport);
	}
}

static unsigned short nr_devices = 10;
module_param(nr_devices, ushort, 0444);

static int __init ttytest_init(void)
{
	int ret;

	ttytest_driver = tty_alloc_driver(MINORMASK + 1,
					  TTY_DRIVER_RESET_TERMIOS |
					  TTY_DRIVER_REAL_RAW |
					  TTY_DRIVER_DYNAMIC_DEV);
	if (IS_ERR(ttytest_driver))
		return PTR_ERR(ttytest_driver);

	ttytest_driver->driver_name = "ttytest";
	ttytest_driver->name = "ttytest";
	ttytest_driver->type = TTY_DRIVER_TYPE_SYSTEM;
	ttytest_driver->subtype = SYSTEM_TYPE_TTY;
	ttytest_driver->init_termios = tty_std_termios;
	ttytest_driver->init_termios.c_lflag &= ~(ECHO | ECHOE | ECHOK | ECHONL);
	ttytest_driver->init_termios.c_oflag = OPOST | OCRNL | ONOCR | ONLRET;
	tty_set_operations(ttytest_driver, &ttytest_ops);

	ret = tty_register_driver(ttytest_driver);
	if (ret < 0)
		goto err_put_driver;

	for (unsigned i = 0; i < nr_devices; i++) {
		struct ttytest_port *tport = ttytest_add_one(i);
		if (IS_ERR(tport))
			goto err_remove_all;
	}

	return 0;
err_remove_all:
	ttytest_remove_all();
	tty_unregister_driver(ttytest_driver);
err_put_driver:
	tty_driver_kref_put(ttytest_driver);
	return ret;
}
module_init(ttytest_init);

static void __exit ttytest_exit(void)
{
	ttytest_remove_all();
	tty_unregister_driver(ttytest_driver);
	tty_driver_kref_put(ttytest_driver);
}
module_exit(ttytest_exit);

MODULE_DESCRIPTION("TEST TTY driver");
MODULE_LICENSE("GPL v2");
