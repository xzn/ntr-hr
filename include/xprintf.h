/*------------------------------------------------------------------------*/
/* Universal string handler for user console interface  (C)ChaN, 2011     */
/*------------------------------------------------------------------------*/

#ifndef _STRFUNC
#define _STRFUNC

#define _USE_XFUNC_OUT	1	/* 1: Use output functions */
#define	_CR_CRLF		0	/* 1: Convert \n ==> \r\n in the output char */
#define _USE_XFUNC_OUT_EXTRA 0

#define _USE_XFUNC_IN	0	/* 1: Use input function */
#define	_LINE_ECHO		1	/* 1: Echo back input chars in xgets function */


#if _USE_XFUNC_OUT
#include <stdarg.h>
// #define xdev_out(func) xfunc_out = (void(*)(unsigned char))(func)
// extern void (*xfunc_out)(unsigned char);
// void xputc (char c, char **outptr, void (*out_func)(unsigned char));
// void xputs (const char* str, char **outptr, void (*out_func)(unsigned char));
// void xvprintf (char **outptr, void (*out_func)(unsigned char), const char* fmt, va_list arp);
void xsprintf (char* buff, const char* fmt, ...);
void xfvprintf (void (*out_func)(char), const char* fmt, va_list arp);
#if _USE_XFUNC_OUT_EXTRA
void xfputs (void (*func)(unsigned char), const char* str);
void xfprintf (void (*func)(unsigned char), const char*	fmt, ...);
void xprintf (char **outptr, void (*out_func)(unsigned char), const char* fmt, ...);
void put_dump (char **outptr, void (*out_func)(unsigned char), const void* buff, unsigned long addr, int len, int width);
#endif
#define DW_CHAR		sizeof(char)
#define DW_SHORT	sizeof(short)
#define DW_LONG		sizeof(long)
#endif

#if _USE_XFUNC_IN
// #define xdev_in(func) xfunc_in = (unsigned char(*)(void))(func)
// extern unsigned char (*xfunc_in)(void);
int xgets (char* buff, int len, unsigned char (*func)(void));
#if _LINE_ECHO
int xgets_echo (char* buff, int len, unsigned char (*func)(void), char **outptr, void (*out_func)(unsigned char));
#endif
// int xfgets (unsigned char (*func)(void), char* buff, int len);
int xatoi (char** str, long* res);
#endif

#endif
