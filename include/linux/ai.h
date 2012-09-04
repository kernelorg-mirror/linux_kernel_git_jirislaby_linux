/*
 * Copyright (C) 2011 Jiri Slaby <jirislaby@gmail.com>
 *
 * Licensed under the GPLv2
 */
#ifndef __LINUX_AI_H
#define __LINUX_AI_H

extern int klee_range(int begin, int end, const char *name);

extern void __assert_fail (__const char *__assertion, __const char *__file,
		unsigned int __line, __const char *__function) __attribute__((__noreturn__));
#define __ai_assert(expr)						\
  ((expr)								\
      ? (void)(0)							\
      : __assert_fail(#expr, __FILE__, __LINE__, __PRETTY_FUNCTION__))

extern volatile int __ai_load(void *lock, const char *what);
extern void __ai_store(void *lock, const char *what, int val);
/* int required for clang checker */
static inline int __ai_lock(void *lock) { return 1; }
static inline void __ai_unlock(void *lock) { }

#define __ai_lock(L) do {				\
	__ai_assert(__ai_load(L, __stringify(L)) == 0);	\
	__ai_store(L, __stringify(L), 1);		\
	__ai_lock(L);					\
} while (0)

#define __ai_lock_cond(L) ({			\
	int __ai_ret = klee_range(0, 2, "");	\
	if (__ai_ret)				\
		__ai_lock(L);			\
	__ai_ret;				\
})

#define __ai_unlock(L) do {				\
	__ai_assert(__ai_load(L, __stringify(L)) == 1);	\
	__ai_store(L, __stringify(L), 0);		\
	__ai_unlock(L);					\
} while (0)

#endif
