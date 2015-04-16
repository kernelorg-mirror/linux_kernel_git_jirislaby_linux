#include <linux/cpumask.h>
#include <linux/kernel.h>
#include <linux/mm.h>
#include <linux/slub_def.h>
#include <linux/workqueue.h>

unsigned my_make_symbolic;
const char *my_make_symbolic_str;

extern void *malloc(size_t size);
extern void free(void *ptr);

extern   __attribute__((noreturn))
void klee_report_error(const char *file, int line, const char *message,
		const char *suffix);
extern void klee_warning(const char *message);
extern int klee_int(const char *name);
extern void klee_make_symbolic(void *addr, size_t nbytes, const char *name);

/**************************************/

#include <linux/sched.h>
struct signal_struct curr_sig;
struct thread_info tinfo;
struct task_struct curr = {
	.signal = &curr_sig,
	.stack = &tinfo,
};
struct task_struct *current_task = &curr;

/* percpu */
struct pt_regs *irq_regs;

int __preempt_count;
struct boot_params boot_params;
atomic_t system_freezing_cnt;
unsigned long kernel_stack;
struct workqueue_struct *system_unbound_wq;
const struct sysfs_ops kobj_sysfs_ops;
struct kobject *fs_kobj;
struct workqueue_struct* system_wq;
volatile unsigned long jiffies;
int oops_in_progress;
int panic_timeout = CONFIG_PANIC_TIMEOUT;
int suid_dumpable = 0;
unsigned long phys_base;

#include <linux/user_namespace.h>
struct user_namespace init_user_ns;
#if 0
#include <linux/init_task.h>
struct pid init_struct_pid = INIT_STRUCT_PID;
#include <linux/fdtable.h>
struct files_struct init_files;
#endif
#include <linux/sched.h>
struct task_struct init_task; // = INIT_TASK(init_task);
struct user_struct root_user;

struct cpuinfo_x86 boot_cpu_data;
struct pglist_data contig_page_data;
struct mem_section *mem_section[NR_SECTION_ROOTS];
//const struct cpumask * const cpu_possible_mask;
int hashdist;
struct tss_struct cpu_tss;

/**************************************/

char *strreplace(char *s, char old, char new)
{
	for (; *s; ++s)
		if (*s == old)
			*s = new;
	return s;
}

size_t strlen(const char *s)
{
	const char *sc;

	for (sc = s; *sc != '\0'; ++sc)
		/* nothing */;
	return sc - s;
}

size_t strlcpy(char *dest, const char *src, size_t size)
{
	size_t ret = strlen(src);

	if (size) {
		size_t len = (ret >= size) ? size - 1 : ret;
		memcpy(dest, src, len);
		dest[len] = '\0';
	}
	return ret;
}

unsigned long _copy_from_user(void *to, const void __user *from, unsigned n)
{
#ifdef BETTER_USER
	memcpy(to, from, n);
	return klee_int("_copy_from_user");
#else
	char buf[n];
	klee_make_symbolic(buf, n, "_copy_from_user");
	memcpy(to, buf, n);
	return 0;
#endif
}

unsigned long _copy_to_user(void __user *to, const void *from, unsigned n)
{
#ifdef BETTER_USER
	memcpy(to, from, n);
	return klee_int("_copy_to_user");
#else
	return 0;
#endif
}

char *kstrdup(const char *s, gfp_t gfp)
{
	size_t len = strlen(s);
	char *ret = malloc(len);
	if (ret)
		memcpy(ret, s, len);
	return ret;
}

void *__kmalloc(size_t size, gfp_t flags)
{
	void *ret = malloc(size);

	if (ret && flags & __GFP_ZERO)
		memset(ret, 0, size);

/*	if (ret && my_make_symbolic--)
		klee_make_symbolic(ret, size, my_make_symbolic_str);*/

	return ret;
}

void kfree(const void *x)
{
	void *a = (void *)x;
	free(a);
}

void *vmalloc(unsigned long size)
{
	return __kmalloc(size, GFP_KERNEL);
}

void vfree(const void *x)
{
	kfree(x);
}

struct kmem_cache *
kmem_cache_create(const char *name, size_t size, size_t align,
		  unsigned long flags, void (*ctor)(void *))
{
	struct kmem_cache *ret;

	ret = malloc(sizeof(*ret));
	if (ret) {
		ret->object_size = size;
		ret->ctor = ctor;
	}

	return ret;
}

void *kmem_cache_alloc(struct kmem_cache *cachep, gfp_t flags)
{
	void *ret;

	if (!cachep)
		klee_report_error(__FILE__, __LINE__, "cachep is NULL",
				"kmem.err");

	ret = malloc(cachep->object_size);
	if (ret) {
		if (flags & __GFP_ZERO)
			memset(ret, 0, cachep->object_size);

		if (cachep->ctor)
			cachep->ctor(ret);
	}

	return ret;
}

void kmem_cache_free(struct kmem_cache *s, void *x)
{
	free(x);
}

unsigned long __get_free_pages(gfp_t gfp_mask, unsigned int order)
{
	return (unsigned long)malloc(4096 * (1 << order));
}

int printk(const char *fmt, ...)
{
//	klee_warning(fmt);
	return 0;
}

void __pr_emerg(const char *fmt, ...) {}
void __pr_alert(const char *fmt, ...) {}
void __pr_crit(const char *fmt, ...) {}
void __pr_err(const char *fmt, ...) {}
void __pr_warn(const char *fmt, ...) {}
void __pr_notice(const char *fmt, ...) {}
void __pr_info(const char *fmt, ...) {}

void warn_slowpath_fmt(const char *file, int line, const char *fmt, ...)
{
	klee_report_error(file, line, fmt, "warn");
}

void warn_slowpath_null(const char *file, int line)
{
	klee_report_error(file, line, "warn_slowpath_null", "warn");
}

#include <linux/ratelimit.h>
int ___ratelimit(struct ratelimit_state *rs, const char *func)
{
	return 0;
}

void __mutex_init(struct mutex *lock, const char *name,
		struct lock_class_key *key)
{
}

void mutex_lock(struct mutex *lock)
{
}

int mutex_lock_interruptible(struct mutex *lock)
{
	return klee_int("mutex_lock_interruptible");
}

int mutex_trylock(struct mutex *lock)
{
	return klee_int("mutex_trylock");
}

void mutex_unlock(struct mutex *lock)
{
}

void down_read(struct rw_semaphore *sem)
{
}

void up_read(struct rw_semaphore *sem)
{
}

void down_write(struct rw_semaphore *sem)
{
}

void up_write(struct rw_semaphore *sem)
{
}

void __init_rwsem(struct rw_semaphore *sem, const char *name,
		  struct lock_class_key *key)
{
}

void __init_waitqueue_head(wait_queue_head_t *q, const char *name,
		struct lock_class_key *key)
{
}

bool queue_work_on(int cpu, struct workqueue_struct *wq,
		   struct work_struct *work)
{
	return klee_int("queue_work_on");
}

void __wake_up(wait_queue_head_t *q, unsigned int mode,
			int nr_exclusive, void *key)
{
}

long prepare_to_wait_event(wait_queue_head_t *q, wait_queue_t *wait, int state)
{
	return 0;
}

void finish_wait(wait_queue_head_t *q, wait_queue_t *wait)
{
}

void add_wait_queue(wait_queue_head_t *q, wait_queue_t *wait)
{
}

void remove_wait_queue(wait_queue_head_t *q, wait_queue_t *wait)
{
}

unsigned long get_seconds(void)
{
	return 10000;
}

void schedule(void)
{
}

signed long schedule_timeout(signed long timeout)
{
	long ret = klee_int("schedule_timeout");
	return ret < 0 ? -ret : ret;
}

bool capable(int cap)
{
	return !!klee_int("capable");
}

pid_t pid_vnr(struct pid *pid) 
{
	return klee_int("pid_vnr");
}

void put_pid(struct pid *pid)
{
}

void kill_fasync(struct fasync_struct **fp, int sig, int band)
{
}

unsigned long msleep_interruptible(unsigned int msecs)
{
	return 0;
}

void msleep(unsigned int msecs)
{
}

int mod_timer(struct timer_list *timer, unsigned long expires)
{
	return 0;
}

int del_timer(struct timer_list *timer)
{
	return 0;
}

int atomic_notifier_call_chain(struct atomic_notifier_head *nh,
			       unsigned long val, void *v)
{
	return NOTIFY_DONE;
}

int input_handler_for_each_handle(struct input_handler *handler, void *data,
				  int (*fn)(struct input_handle *, void *))
{
	return 0;
}

void __tasklet_schedule(struct tasklet_struct *t)
{
}
