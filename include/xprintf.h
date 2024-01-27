#ifndef XPRINTF_H
#define XPRINTF_H

#include <stdarg.h>
#include <stddef.h>
int xsprintf(char *buf, const char *fmt, ...);
int xsnprintf(char *buf, size_t buff_len, const char *fmt, ...);
int strnjoin(char *dst, size_t dst_len, const char *s1, const char *s2);

#endif
