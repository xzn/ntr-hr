/*------------------------------------------------------------------------/
/  Universal string handler for user console interface
/-------------------------------------------------------------------------/
/
/  Copyright (C) 2011, ChaN, all right reserved.
/
/ * This software is a free software and there is NO WARRANTY.
/ * No restriction on use. You can use, modify and redistribute it for
/   personal, non-profit or commercial products UNDER YOUR RESPONSIBILITY.
/ * Redistributions of source code must retain the above copyright notice.
/
/-------------------------------------------------------------------------*/

#include "xprintf.h"


#if _USE_XFUNC_OUT
// void (*xfunc_out)(unsigned char);	/* Pointer to the output stream */
// static char *outptr;

/*----------------------------------------------*/
/* Put a character                              */
/*----------------------------------------------*/

static inline void xputc (char c, char **outptr, void (*out_func)(char))
{
	if (_CR_CRLF && c == '\n') xputc('\r', outptr, out_func);		/* CR -> CRLF */

	if (outptr) {
		if (*outptr) {
			*(*outptr)++ = (unsigned char)c;
			return;
		}
	}

	if (out_func) out_func((unsigned char)c);
	// else if (xfunc_out) xfunc_out((unsigned char)c);
}


/*----------------------------------------------*/
/* Put a null-terminated string                 */
/*----------------------------------------------*/

static inline void xputs (					/* Put a string to the default device */
	const char* str,			/* Pointer to the string */
	char **outptr, void (*out_func)(char)
)
{
	while (*str)
		xputc(*str++, outptr, out_func);
}


/*----------------------------------------------*/
/* Formatted string output                      */
/*----------------------------------------------*/
/*  xprintf("%d", 1234);			"1234"
    xprintf("%6d,%3d%%", -200, 5);	"  -200,  5%"
    xprintf("%-6u", 100);			"100   "
    xprintf("%ld", 12345678L);		"12345678"
    xprintf("%04x", 0xA3);			"00a3"
    xprintf("%08LX", 0x123ABC);		"00123ABC"
    xprintf("%016b", 0x550F);		"0101010100001111"
    xprintf("%s", "String");		"String"
    xprintf("%-4s", "abc");			"abc "
    xprintf("%4s", "abc");			" abc"
    xprintf("%c", 'a');				"a"
    xprintf("%f", 10.0);            <xprintf lacks floating point support>
*/

static inline void xvprintf (
	char **outptr, void (*out_func)(char),
	const char*	fmt,	/* Pointer to the format string */
	va_list arp			/* Pointer to arguments */
)
{
	unsigned int r, i, j, w, f;
	unsigned long v;
	char s[16], c, d, *p;


	for (;;) {
		c = *fmt++;					/* Get a char */
		if (!c) break;				/* End of format? */
		if (c != '%') {				/* Pass through it if not a % sequense */
			xputc(c, outptr, out_func); continue;
		}
		f = 0;
		c = *fmt++;					/* Get first char of the sequense */
		if (c == '0') {				/* Flag: '0' padded */
			f = 1; c = *fmt++;
		} else {
			if (c == '-') {			/* Flag: left justified */
				f = 2; c = *fmt++;
			}
		}
		for (w = 0; c >= '0' && c <= '9'; c = *fmt++)	/* Minimum width */
			w = w * 10 + c - '0';
		if (c == 'l' || c == 'L') {	/* Prefix: Size is long int */
			f |= 4; c = *fmt++;
		}
		if (!c) break;				/* End of format? */
		d = c;
		if (d >= 'a') d -= 0x20;
		switch (d) {				/* Type is... */
		case 'S' :					/* String */
			p = va_arg(arp, char*);
			for (j = 0; p[j]; j++) ;
			while (!(f & 2) && j++ < w) xputc(' ', outptr, out_func);
			xputs(p, outptr, out_func);
			while (j++ < w) xputc(' ', outptr, out_func);
			continue;
		case 'C' :					/* Character */
			xputc((char)va_arg(arp, int), outptr, out_func); continue;
		case 'B' :					/* Binary */
			r = 2; break;
		case 'O' :					/* Octal */
			r = 8; break;
		case 'D' :					/* Signed decimal */
		case 'U' :					/* Unsigned decimal */
			r = 10; break;
		case 'X' :					/* Hexdecimal */
			r = 16; break;
		default:					/* Unknown type (passthrough) */
			xputc(c, outptr, out_func); continue;
		}

		/* Get an argument and put it in numeral */
		v = (f & 4) ? va_arg(arp, long) : ((d == 'D') ? (long)va_arg(arp, int) : (long)va_arg(arp, unsigned int));
		if (d == 'D' && (v & 0x80000000)) {
			v = 0 - v;
			f |= 8;
		}
		i = 0;
		do {
			d = (char)(v % r); v /= r;
			if (d > 9) d += (c == 'x') ? 0x27 : 0x07;
			s[i++] = d + '0';
		} while (v && i < sizeof(s));
		if (f & 8) s[i++] = '-';
		j = i; d = (f & 1) ? '0' : ' ';
		while (!(f & 2) && j++ < w) xputc(d, outptr, out_func);
		do xputc(s[--i], outptr, out_func); while(i);
		while (j++ < w) xputc(' ', outptr, out_func);
	}
}


void xfvprintf (void (*out_func)(char), const char* fmt, va_list arp)
{
	xvprintf(0, out_func, fmt, arp);
}


void xsprintf (			/* Put a formatted string to the memory */
	char* buff,			/* Pointer to the output buffer */
	const char*	fmt,	/* Pointer to the format string */
	...					/* Optional arguments */
)
{
	va_list arp;


	// outptr = buff;		/* Switch destination for memory */

	va_start(arp, fmt);
	xvprintf(&buff, 0, fmt, arp);
	va_end(arp);

	*buff = 0;

	// *outptr = 0;		/* Terminate output string with a \0 */
	// outptr = 0;			/* Switch destination for device */
}


#if _USE_XFUNC_OUT_EXTRA
void xfputs (					/* Put a string to the specified device */
	void(*func)(unsigned char),	/* Pointer to the output function */
	const char*	str				/* Pointer to the string */
)
{
	// void (*pf)(unsigned char);


	// pf = xfunc_out;		/* Save current output device */
	// xfunc_out = func;	/* Switch output to specified device */
	while (*str)		/* Put the string */
		xputc(*str++, 0, func);
	// xfunc_out = pf;		/* Restore output device */
}


void xfprintf (					/* Put a formatted string to the specified device */
	void(*func)(unsigned char),	/* Pointer to the output function */
	const char*	fmt,			/* Pointer to the format string */
	...							/* Optional arguments */
)
{
	va_list arp;
	// void (*pf)(unsigned char);


	// pf = xfunc_out;		/* Save current output device */
	// xfunc_out = func;	/* Switch output to specified device */

	va_start(arp, fmt);
	xvprintf(0, func, fmt, arp);
	va_end(arp);

	// xfunc_out = pf;		/* Restore output device */
}


void xprintf (			/* Put a formatted string to the default device */
	char **outptr, void (*out_func)(unsigned char),
	const char*	fmt,	/* Pointer to the format string */
	...					/* Optional arguments */
)
{
	va_list arp;


	va_start(arp, fmt);
	xvprintf(outptr, out_func, fmt, arp);
	va_end(arp);
}



/*----------------------------------------------*/
/* Dump a line of binary dump                   */
/*----------------------------------------------*/

void put_dump (
	char **outptr, void (*out_func)(unsigned char),
	const void* buff,		/* Pointer to the array to be dumped */
	unsigned long addr,		/* Heading address value */
	int len,				/* Number of items to be dumped */
	int width				/* Size of the items (DF_CHAR, DF_SHORT, DF_LONG) */
)
{
	int i;
	const unsigned char *bp;
	const unsigned short *sp;
	const unsigned long *lp;


	xprintf(outptr, out_func, "%08lX ", addr);		/* address */

	switch (width) {
	case DW_CHAR:
		bp = buff;
		for (i = 0; i < len; i++)		/* Hexdecimal dump */
			xprintf(outptr, out_func, " %02X", bp[i]);
		xputc(' ', outptr, out_func);
		for (i = 0; i < len; i++)		/* ASCII dump */
			xputc((bp[i] >= ' ' && bp[i] <= '~') ? bp[i] : '.', outptr, out_func);
		break;
	case DW_SHORT:
		sp = buff;
		do								/* Hexdecimal dump */
			xprintf(outptr, out_func, " %04X", *sp++);
		while (--len);
		break;
	case DW_LONG:
		lp = buff;
		do								/* Hexdecimal dump */
			xprintf(outptr, out_func, " %08LX", *lp++);
		while (--len);
		break;
	}

	xputc('\n', outptr, out_func);
}
#endif

#endif /* _USE_XFUNC_OUT */



#if _USE_XFUNC_IN
unsigned char (*xfunc_in)(void);	/* Pointer to the input stream */

/*----------------------------------------------*/
/* Get a line from the input                    */
/*----------------------------------------------*/

#if _LINE_ECHO
inline int xgets_echo (char* buff, int len, unsigned char (*func)(void), char **outptr, void (*out_func)(unsigned char))
{
	int c, i;


	if (!func) return 0;		/* No input function specified */

	i = 0;
	for (;;) {
		c = func();				/* Get a char from the incoming stream */
		if (!c) return 0;			/* End of stream? */
		if (c == '\r') break;		/* End of line? */
		if (c == '\b' && i) {		/* Back space? */
			i--;
			xputc(c, outptr, out_func);
			continue;
		}
		if (c >= ' ' && i < len - 1) {	/* Visible chars */
			buff[i++] = c;
			xputc(c, outptr, out_func);
		}
	}
	buff[i] = 0;	/* Terminate with a \0 */
	xputc('\n', outptr, out_func);
	return 1;
}
#endif

int xgets (		/* 0:End of stream, 1:A line arrived */
	char* buff,	/* Pointer to the buffer */
	int len,	/* Buffer length */
	unsigned char (*func)(void)
)
{
	xgets_echo (buff, len, func, 0, 0);
}


#if 0
int xfgets (	/* 0:End of stream, 1:A line arrived */
	unsigned char (*func)(void),	/* Pointer to the input stream function */
	char* buff,	/* Pointer to the buffer */
	int len		/* Buffer length */
)
{
	unsigned char (*pf)(void);
	int n;


	// pf = xfunc_in;			/* Save current input device */
	// xfunc_in = func;		/* Switch input to specified device */
	n = xgets(buff, len, func);	/* Get a line */
	// xfunc_in = pf;			/* Restore input device */

	return n;
}
#endif


/*----------------------------------------------*/
/* Get a value of the string                    */
/*----------------------------------------------*/
/*	"123 -5   0x3ff 0b1111 0377  w "
	    ^                           1st call returns 123 and next ptr
	       ^                        2nd call returns -5 and next ptr
                   ^                3rd call returns 1023 and next ptr
                          ^         4th call returns 15 and next ptr
                               ^    5th call returns 255 and next ptr
                                  ^ 6th call fails and returns 0
*/

int xatoi (			/* 0:Failed, 1:Successful */
	char **str,		/* Pointer to pointer to the string */
	long *res		/* Pointer to the valiable to store the value */
)
{
	unsigned long val;
	unsigned char c, r, s = 0;


	*res = 0;

	while ((c = **str) == ' ') (*str)++;	/* Skip leading spaces */

	if (c == '-') {		/* negative? */
		s = 1;
		c = *(++(*str));
	}

	if (c == '0') {
		c = *(++(*str));
		switch (c) {
		case 'x':		/* hexdecimal */
			r = 16; c = *(++(*str));
			break;
		case 'b':		/* binary */
			r = 2; c = *(++(*str));
			break;
		default:
			if (c <= ' ') return 1;	/* single zero */
			if (c < '0' || c > '9') return 0;	/* invalid char */
			r = 8;		/* octal */
		}
	} else {
		if (c < '0' || c > '9') return 0;	/* EOL or invalid char */
		r = 10;			/* decimal */
	}

	val = 0;
	while (c > ' ') {
		if (c >= 'a') c -= 0x20;
		c -= '0';
		if (c >= 17) {
			c -= 7;
			if (c <= 9) return 0;	/* invalid char */
		}
		if (c >= r) return 0;		/* invalid char for current radix */
		val = val * r + c;
		c = *(++(*str));
	}
	if (s) val = 0 - val;			/* apply sign if needed */

	*res = val;
	return 1;
}

#endif /* _USE_XFUNC_IN */
