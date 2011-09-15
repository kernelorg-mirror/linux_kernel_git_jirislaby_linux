#include <linux/module.h>
#include <linux/mutex.h>

static DEFINE_MUTEX(M);

void fun(int a)
{
	mutex_lock(&M);
	if (a) {
		mutex_unlock(&M);
		return;
	}
	mutex_unlock(&M);
}
