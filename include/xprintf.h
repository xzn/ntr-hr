#ifndef XPRINTF_H
#define XPRINTF_H

#include <stdarg.h>
#include <stddef.h>
int xsprintf(char *buf, const char *fmt, ...) __attribute__((format(printf, 2, 3)));
int xsnprintf(char *buf, size_t buff_len, const char *fmt, ...) __attribute__((format(printf, 3, 4)));
int strnjoin(char *dst, size_t dst_len, const char *s1, const char *s2);

#endif
