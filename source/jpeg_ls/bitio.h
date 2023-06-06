/* SPMG/JPEG-LS IMPLEMENTATION V.2.1
   =====================================
   These programs are Copyright (c) University of British Columbia. All rights reserved.
   They may be freely redistributed in their entirety provided that this copyright
   notice is not removed. THEY MAY NOT BE SOLD FOR PROFIT OR INCORPORATED IN
   OMMERCIAL PROGRAMS WITHOUT THE WRITTEN PERMISSION OF THE COPYRIGHT HOLDER.
   Each program is provided as is, without any express or implied warranty,
   without even the warranty of fitness for a particular purpose.

   =========================================================
   THIS SOFTWARE IS BASED ON HP's implementation of jpeg-ls:
   =========================================================

   LOCO-I/JPEG-LS IMPLEMENTATION V.0.90
   -------------------------------------------------------------------------------
   (c) COPYRIGHT HEWLETT-PACKARD COMPANY, 1995-1999.
	   HEWLETT-PACKARD COMPANY ("HP") DOES NOT WARRANT THE ACCURACY OR
   COMPLETENESS OF THE INFORMATION GIVEN HERE.  ANY USE MADE OF, OR
   RELIANCE ON, SUCH INFORMATION IS ENTIRELY AT USER'S OWN RISK.
	   BY DOWNLOADING THE LOCO-I/JPEG-LS COMPRESSORS/DECOMPRESSORS
   ("THE SOFTWARE") YOU AGREE TO BE BOUND BY THE TERMS AND CONDITIONS
   OF THIS LICENSING AGREEMENT.
	   YOU MAY DOWNLOAD AND USE THE SOFTWARE FOR NON-COMMERCIAL PURPOSES
   FREE OF CHARGE OR FURTHER OBLIGATION.  YOU MAY NOT, DIRECTLY OR
   INDIRECTLY, DISTRIBUTE THE SOFTWARE FOR A FEE, INCORPORATE THIS
   SOFTWARE INTO ANY PRODUCT OFFERED FOR SALE, OR USE THE SOFTWARE
   TO PROVIDE A SERVICE FOR WHICH A FEE IS CHARGED.
	   YOU MAY MAKE COPIES OF THE SOFTWARE AND DISTRIBUTE SUCH COPIES TO
   OTHER PERSONS PROVIDED THAT SUCH COPIES ARE ACCOMPANIED BY
   HEWLETT-PACKARD'S COPYRIGHT NOTICE AND THIS AGREEMENT AND THAT
   SUCH OTHER PERSONS AGREE TO BE BOUND BY THE TERMS OF THIS AGREEMENT.
	   THE SOFTWARE IS NOT OF PRODUCT QUALITY AND MAY HAVE ERRORS OR DEFECTS.
   THE JPEG-LS STANDARD IS STILL UNDER DEVELOPMENT. THE SOFTWARE IS NOT A
   FINAL OR FULL IMPLEMENTATION OF THE STANDARD.  HP GIVES NO EXPRESS OR
   IMPLIED WARRANTY OF ANY KIND AND ANY IMPLIED WARRANTIES OF
   MERCHANTABILITY AND FITNESS FOR PURPOSE ARE DISCLAIMED.
	   HP SHALL NOT BE LIABLE FOR ANY DIRECT, INDIRECT, SPECIAL, INCIDENTAL,
   OR CONSEQUENTIAL DAMAGES ARISING OUT OF ANY USE OF THE SOFTWARE.
   -------------------------------------------------------------------------------
*/

/* bitio.h --- for I/O routines
 *
 * Initial code by Alex Jakulin,  Aug. 1995
 *
 * Modified and optimized: Gadiel Seroussi, October 1995
 *
 * Color Enhancement: Guillermo Sapiro, August 1996
 *
 * Modified and added Restart marker and input tables by:
 * David Cheng-Hsiu Chu, and Ismail R. Ismail march 1999
 */

#ifndef BITIO_H
#define BITIO_H

#include <stdint.h>
#include <string.h>


#define ESCAPE 0

struct bito_ctx {

	uint8_t *buf, *buf_end;

	/* BIT I/O variables */
	uint32_t reg;         /* BIT buffer for input/output */
	int bits;          /* number of bits free in bit buffer (on output) */
	/* (number of bits free)-8 in bit buffer (on input)*/
#define BITBUFSIZE (8*sizeof(reg))

	void *user;
	int (*flush)(struct bito_ctx *);

};

#define myputc(bctx, c) \
{ \
	if (bctx->buf >= bctx->buf_end) { \
		int ret; \
		if ((ret = flushbuff(bctx))) \
			return ret; \
	} \
	*bctx->buf++ = c; \
}

#define assert(...)

static inline int flushbuff(struct bito_ctx *bctx) {
	/* mywrite must work correctly, even if fp is equal to 0 */
	return bctx->flush(bctx);
}


#define put_zeros(ctx,bctx,n)                                          \
{                                                             \
	bctx->bits -= n;                                            \
	while (bctx->bits <= 24) {                                  \
		if (bctx->buf >= bctx->buf_end) {                          \
			int ret; \
			if ((ret = flushbuff(bctx))) \
				return ret;       \
		}                                             \
		*bctx->buf++ = bctx->reg >> 24;                 \
		bctx->reg <<= 8;                                    \
		bctx->bits += 8;                                    \
	}                                                     \
}

#define PUT_ZEROS(ctx,bctx,n) put_zeros(ctx,bctx,n)


#define put_ones(ctx,bctx,n)                                             \
{                                                               \
	if ( n < 24 ) {						\
		putbits(ctx,bctx,(1<<n)-1,n);				\
	}							\
	else {							\
		register unsigned nn = n;				\
		while ( nn >= 24 ) {				\
		putbits(ctx,bctx,(1<<24)-1,24);				\
		nn -= 24;					\
		}							\
		if ( nn ) putbits(ctx,bctx,(1<<nn)-1,nn);			\
	}							\
}

#define PUT_ONES(ctx,bctx,n) put_ones(ctx,bctx,n)


/*
 * Put an n-bit number x in the output stream (inline code).
 * Check for output bytes of the form 0xff and stuff
 * a 0 bit following each one.
 */

#define putbits(ctx, bctx, x, n)                                           \
{								\
	assert(n <= 24 && n >= 0 && ((1<<n)>x));		\
	bctx->bits -= n;                                              \
	bctx->reg |= x << bctx->bits;                                       \
	while (bctx->bits <= 24) {                                   	\
		uint8_t outbyte;		\
		if (bctx->buf >= bctx->buf_end) {                       		\
			int ret; \
			if ((ret = flushbuff(bctx))) \
				return ret;       \
		}                                       \
		outbyte = (*bctx->buf++ = (bctx->reg >> 24) );		\
		if ( ESCAPE && outbyte == 0xff ) {		\
			bctx->bits += 7;			\
			bctx->reg <<= 7;			\
						/* stuff a 0 at MSB */          \
			bctx->reg &= ~(1<<(8*sizeof(bctx->reg)-1)); \
		}					\
		else {					\
			bctx->bits += 8;                      \
			bctx->reg <<= 8;                      \
		}					\
	}                                                       \
}


#define PUTBITS(ctx,bctx,x,n) putbits(ctx,bctx,x,n)


#endif /* BITIO_H */
