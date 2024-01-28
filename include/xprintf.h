#ifndef XPRINTF_H
#define XPRINTF_H

#include <stdarg.h>
#include <stddef.h>
size_t xsprintf(char *buf, const char *fmt, ...) __attribute__((format(printf, 2, 3)));
size_t xsnprintf(char *buf, size_t buff_len, const char *fmt, ...) __attribute__((format(printf, 3, 4)));

/** Holds a user defined output stream */
struct ostrm
{
	/** Parameter to pass the the function. */
	void *p;
	/** Send a memory block ta a user defined stream.
	 * @param p   Parameter.
	 * @param src Source memory block.
	 * @param len Length in bytes of the memory block. */
	void (*func)(void *p, void const *src, size_t len);
};

size_t xprintf(struct ostrm const* o, char const* fmt, ... ) __attribute__((format(printf, 2, 3)));
size_t xvprintf(struct ostrm const* o, char const* fmt, va_list va);

int strnjoin(char *dst, size_t dst_len, const char *s1, const char *s2);

#endif
