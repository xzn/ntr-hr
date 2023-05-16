/* SPMG/JPEG-LS IMPLEMENTATION V.2.1
   =====================================
   These programs are Copyright (c) University of British Columbia. All rights reserved.
   They may be freely redistributed in their entirety provided that this copyright
   notice is not removed. THEY MAY NOT BE SOLD FOR PROFIT OR INCORPORATED IN
   COMMERCIAL PROGRAMS WITHOUT THE WRITTEN PERMISSION OF THE COPYRIGHT HOLDER.
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

/* global.h --- prototypes for functions and global variables
 *
 * Initial code by Alex Jakulin,  Aug. 1995
 *
 * Modified and optimized: Gadiel Seroussi, October 1995
 *
 * Modified and added Restart marker and input tables by:
 * David Cheng-Hsiu Chu, and Ismail R. Ismail march 1999
 *
 */

#ifndef GLOBAL_H
#define GLOBAL_H

#include <stddef.h>
#include <stdint.h>

#ifndef CLOCKS_PER_SEC
#define CLOCKS_PER_SEC 10e6
#endif

/*#define NDEBUG*/
#define POW2
// #define FIXALPHA
#define FIXRESET


/* TRUE and FALSE values */
#define TRUE 1
#define FALSE 0


/* Version number */
#define JPEGLSVERSION	"V.2.1"


/* Maximal number of components in the implementation*/
#define MAX_COMPONENTS	components
#define MAX_SCANS	MAX_COMPONENTS


#ifndef BIG_ENDIAN
#define BIG_ENDIAN	1
#endif

struct jls_enc_ctx {

#define components 1

#define NAME_LENGTH	40

/* Output file names */
#define OUTFILE "outfile"
#define COMPSUFFIX ".jls"


/* Define max and min macros */
#ifndef max
#	define max(a,b)  (((a)>=(b))?(a):(b))
#	define min(a,b)  (((a)<=(b))?(a):(b))
#endif


/****** Constants */

/* margins for scan lines */
#define LEFTMARGIN 1
#define RIGHTMARGIN	1


/* alphabet size */
#define MAXA8 (256)
#define MAXA16 (65536)
#define LUTMAX8 (256)
#define LUTMAX16 (4501)


#ifdef FIXALPHA
#  ifndef alpha
#    define	alpha	256
#  endif
#  define 	highmask (-(alpha))
#  ifndef POW2
#	define POW2
#  endif
#  if (alpha!=2) && (alpha!=4) && (alpha!=8) && (alpha!=16) && (alpha!=32) &&\
     (alpha!=64) && ( alpha!=128) && (alpha!=256) && (alpha!=512) &&\
     (alpha!=1024) && ( alpha!=2048) && (alpha!=4096) && (alpha!=8192) &&\
     (alpha!=16384) && ( alpha!=32768) && (alpha!=65536)
#   	 error "Fixed alpha must be a power of 2"
#  endif
#  define  	ceil_half_alpha (alpha/2)
#define ALPHA(ctx) alpha
#define HIGHMASK(ctx) highmask
#define CEIL_HALF_ALPHA(ctx) ceil_half_alpha
#else
int      alpha;     /* alphabet size */
int      ceil_half_alpha; /* ceil(alpha/2) */
int      highmask;  /* for powers of 2, a mask for high bits */
#define ALPHA(ctx) (ctx->alpha)
#define HIGHMASK(ctx) (ctx->highmask)
#define CEIL_HALF_ALPHA(ctx) (ctx->ceil_half_alpha)
#endif



int bpp,			/* bits per sample */
           qbpp,		/* bits per sample for quantized prediction errors */
           limit,		/* limit for unary part of Golomb code */
           limit_reduce;	/* reduction on above for EOR states */


#define DEF_NEAR	0

/* loss tolerance */
#undef NEAR
#define NEAR DEF_NEAR


/* Quantization threshold basic defaults */
/* These are the defaults for LOSSLESS, 8 bpp. Defaults for other
   cases are derived from these basic values */
#define	BASIC_T1	3
#define	BASIC_T2	7
#define	BASIC_T3	21
#define	BASIC_Ta	5

#define CREGIONS (9)    /* quantization regions for d-b, b-c, c-a */

/* run-length treshold */
#ifndef MAXRUN
#	define MAXRUN (64)
#endif

#define EOLINE	 1
#define NOEOLINE 0

/* number of different contexts */
#define CONTEXTS1 (CREGIONS*CREGIONS*CREGIONS)

#define CONTEXTS   ((CONTEXTS1+1)/2) /* all regions, with symmetric merging */


/* Mandatory for JPEG-LS: */
#define CLAMP
#define CLAMPB
#define CLAMPC


#define MAX_C 127
#define MIN_C -128


#define MAXCODE (N_R_L_ERROR)


/* Number of end-of-run contexts */
#define EOR_CONTEXTS 2


/* Total number of contexts */
#define TOT_CONTEXTS (CONTEXTS +  EOR_CONTEXTS)


/* index of first end-of-run context */
#define EOR_0	(CONTEXTS)


/* index of run state */
#define RUNSTATE 0



/*** offsets */

/* The longest code the bit IO can facilitate */
#define MAXCODELEN 24

/* The stat initialization values */
#define INITNSTAT 1			/* init value for N[] */
#define MIN_INITABSTAT 2    /* min init value for A[] */
#define INITABSLACK 6       /* init value for A is roughly
							   2^(bpp-INITABSLACK) but not less than above */
#define INITBIASTAT 0		/* init value for B[] */

/* Limit for unary code */
#define LIMIT 23

/* reset values */
#define DEFAULT_RESET 64
#define MINRESET 3

#ifdef FIXRESET
#   ifndef RESET
#		define RESET     DEFAULT_RESET
#   endif
#define	reset(ctx)	RESET
#else
int	RESET;
#define	reset(ctx)	(ctx->RESET)
#endif

#define RESRUN    256

/****** Global variables prototypes */

char	*out;
int  	T1, T2, T3, Ta;

#define bpp16 FALSE
#define lossy FALSE


/* for look-up-tables */
int vLUT[3][2 * LUTMAX16];
#define lutmax LUTMAX8
int classmap[CONTEXTS1];

/* statistics tables */
int	N[TOT_CONTEXTS],
			A[TOT_CONTEXTS],
			B[TOT_CONTEXTS],
			C[TOT_CONTEXTS];

};


/*extern byte getk[65][3000];*/
/*extern int clipPx[510];*/


/****** Type prototypes */

/* Portability types */
typedef uint8_t byte;
typedef uint16_t word;
typedef uint32_t dword;

typedef byte pixel;


/****** Function prototypes */

struct bito_ctx;
/* global.c */
void set_thresholds(int alfa, int *T1p, int *T2p, int *T3p);

/* lossless.c */
void lossless_doscanline(struct jls_enc_ctx *, struct bito_ctx *, const pixel *psl, const pixel *sl, int no);

/* bitio.c */
void bitoflush(struct bito_ctx *, char *);
void bitoinit(struct bito_ctx *);

/*  melcode.c */
void init_process_run(int);
void close_process_run();
void  process_run(struct jls_enc_ctx *, struct bito_ctx *, int,int);

/* initialize.c */
int prepareLUTs(struct jls_enc_ctx *);
void prepare_qtables(int);
void init_stats(struct jls_enc_ctx *);

int jpeg_ls_encode(struct jls_enc_ctx *ctx, struct bito_ctx *bctx, char *dst, const pixel *src, int w, int h, int pitch, int bpp);

#ifdef BIG_ENDIAN
#    define ENDIAN8(x)   (x)
#    define ENDIAN16(x)   (x)
#else
#    define ENDIAN8(x) (x&0x000000ff)
#    define ENDIAN16(x) ( ((x>>8)|(x<<8)) & 0x0000ffff)
#endif

/* ENDIAN function to fix endian of PCs (for 8 bit pixels)
#define ENDIAN8(x) (x&0x000000ff)*/


/* ENDIAN function to fix endian of PCs (for 16 bit pixels)
#define ENDIAN16(x) ( ((x>>8)|(x<<8)) & 0x0000ffff )*/



/* clipping macro */
#ifdef POW2
#	define clip(ctx,x) \
	    if ( x & HIGHMASK(ctx) ) {\
	      if(x < 0) \
			x = 0;\
	      else \
			x = ALPHA(ctx) - 1;\
	    }
#else
#	define clip(ctx,x) \
	  if(x < 0)  \
	    x = 0; \
	  else if (x >= ALPHA(ctx)) \
	    x = ALPHA(ctx) - 1;
#endif  /* POW2 */



/* macro to predict Px */
#define predict(Rb, Ra, Rc)	\
{	\
	register pixel minx;	\
	register pixel maxx;	\
	\
	if (Rb > Ra) {	\
		minx = Ra;	\
		maxx = Rb;	\
	} else {	\
		maxx = Ra;	\
		minx = Rb;	\
	}	\
	if (Rc >= maxx)	\
		Px = minx;	\
	else if (Rc <= minx)	\
		Px = maxx;	\
	else	\
		Px = Ra + Rb - Rc;	\
}

#endif
