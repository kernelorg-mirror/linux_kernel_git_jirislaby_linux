/*
 * Copyright (C) 2011 Jiri Slaby <jirislaby@gmail.com>
 *
 * Licensed under the GPLv2
 */
#ifndef __LINUX_AI_H
#define __LINUX_AI_H

extern void __assert_fail (__const char *__assertion, __const char *__file,
		unsigned int __line, __const char *__function) __attribute__((__noreturn__));
#define __ai_assert(expr)						\
  ((expr)								\
      ? (void)(0)							\
      : __assert_fail(#expr, __FILE__, __LINE__, __PRETTY_FUNCTION__))

extern volatile int __ai_load(void *lock, const char *what);
extern void __ai_store(void *lock, const char *what, int val);

#define __ai_lock(L) do {				\
	__ai_assert(__ai_load(L, __stringify(L)) == 0);	\
	__ai_store(L, __stringify(L), 1);		\
} while (0)

#define __ai_unlock(L) do {				\
	__ai_assert(__ai_load(L, __stringify(L)) == 1);	\
	__ai_store(L, __stringify(L), 0);		\
} while (0)

#endif
