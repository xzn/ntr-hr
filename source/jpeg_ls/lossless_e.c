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

/* lossless_e.c --- the main pipeline which processes a scanline by doing
 *                prediction, context computation, context quantization,
 *                and statistics gathering.
 *
 * Initial code by Alex Jakulin,  Aug. 1995
 *
 * Modified and optimized: Gadiel Seroussi, October 1995
 *
 * Modified and added Restart marker and input tables by:
 * David Cheng-Hsiu Chu, and Ismail R. Ismail march 1999
 */

#include <stdio.h>
#include <math.h>

#include "global.h"
#include "bitio.h"


#define CHECK_RET(e) \
{ \
	int ret_val; \
	if ((ret_val = (e))) \
		return ret_val; \
}


/* Do Golomb statistics and ENCODING for LOSS-LESS images */
static inline int lossless_regular_mode(const struct jls_enc_params *params, struct jls_enc_ctx *ctx, struct bito_ctx *bctx, int Q, int SIGN, int Px, pixel Ix)
{
	int At, Nt, Bt, absErrval, Errval, MErrval;
	int	unary;
	int temp;
	uint8_t k;

	Nt = ctx->N[Q];
    At = ctx->A[Q];


	/* Prediction correction (A.4.2), compute prediction error (A.4.3)
	   , and error quantization (A.4.4) */
	Px = Px + (SIGN) * ctx->C[Q];
/*Px = clipPx[Px+127];*/
	clip(params,Px);
	Errval = SIGN * (Ix - Px);


	/* Modulo reduction of predication error (A.4.5) */
	if (Errval < 0)
		Errval += ALPHA(params);     /* Errval is now in [0.. alpha-1] */


	/* Estimate k - Golomb coding variable computation (A.5.1) */
	{
	    register int nst = Nt;
		for(k=0; nst < At; nst<<=1, k++);
	}
/*k=getk[Nt][At];*/


	/* Do Rice mapping and compute magnitude of Errval */
	Bt = ctx->B[Q];

	/* Error Mapping (A.5.2) */
	temp = ( k==0 && ((Bt<<1) <= -Nt) );
	if (Errval >= CEIL_HALF_ALPHA(params)) {
		Errval -= ALPHA(params);
		absErrval = -Errval;
		MErrval = (absErrval<<1) - 1 - temp;
	} else {
		absErrval = Errval;
		MErrval = (Errval<<1) + temp;
	}


	/* update bias stats (after correction of the difference) (A.6.1) */
	ctx->B[Q] = (Bt += Errval);


	/* update Golomb stats */
	ctx->A[Q] += absErrval;


	/* check for reset */
	if (Nt == reset(ctx)) {
	/* reset for Golomb and bias cancelation at the same time */
		ctx->N[Q] = (Nt >>= 1);
		ctx->A[Q] >>= 1;
		ctx->B[Q] = (Bt >>= 1);
	}
	ctx->N[Q] = (++Nt);


	/* Do bias estimation for NEXT pixel */
	/* Bias cancelation tries to put error in (-1,0] (A.6.2)*/
	if  ( Bt <= -Nt ) {

	    if (ctx->C[Q] > MIN_C)
			--ctx->C[Q];

	    if ( (ctx->B[Q] += Nt) <= -Nt )
			ctx->B[Q] = -Nt+1;

	} else if ( Bt > 0 ) {

	    if (ctx->C[Q] < MAX_C)
			++ctx->C[Q];

		if ( (ctx->B[Q] -= Nt) > 0 )
			ctx->B[Q] = 0;
	}


	/* Actually output the code: Mapped Error Encoding (Appendix G) */
	unary = MErrval >> k;
	if ( unary < params->limit ) {
	    put_zeros(ctx,bctx,unary);
		putbits(ctx,bctx,(1 << k) + (MErrval & ((1 << k) - 1)), k + 1);
	}
	else {
	    put_zeros(ctx,bctx,params->limit);
	    putbits(ctx,bctx,(1<<params->qbpp) + MErrval - 1, params->qbpp+1);
	}

	return 0;
}




/* Do end of run encoding for LOSSLESS images */
static inline int lossless_end_of_run(const struct jls_enc_params *params, struct jls_enc_ctx *ctx, struct bito_ctx *bctx, pixel Ra, pixel Rb, pixel Ix, int RItype)
{
	int Errval,
		MErrval,
		Q,
		absErrval,
		oldmap,
		k,
		At,
		unary;

	register int Nt;

	Q = EOR_0 + RItype;
	Nt = ctx->N[Q];
	At = ctx->A[Q];

	Errval = Ix - Rb;
	if (RItype)
		At += Nt>>1;
	else {
		if ( Rb < Ra )
			Errval = -Errval;
	}


	/* Estimate k */
	for(k=0; Nt < At; Nt<<=1, k++);

	if (Errval < 0)
		Errval += ALPHA(params);
	if( Errval >= CEIL_HALF_ALPHA(params) )
		Errval -= ALPHA(params);


	oldmap = ( k==0 && Errval && (ctx->B[Q]<<1)<Nt );
	/*  Note: the Boolean variable 'oldmap' is not
		identical to the variable 'map' in the
		JPEG-LS draft. We have
		oldmap = (Errval<0) ? (1-map) : map;
	*/

	/* Error mapping for run-interrupted sample (Figure A.22) */
	if( Errval < 0) {
		MErrval = -(Errval<<1)-1-RItype+oldmap;
		ctx->B[Q]++;
	}else
		MErrval = (Errval<<1)-RItype-oldmap;

	absErrval = (MErrval+1-RItype)>>1;

	/* Update variables for run-interruped sample (Figure A.23) */
	ctx->A[Q] += absErrval;
	if (ctx->N[Q] == reset(ctx)) {
		ctx->N[Q] >>= 1;
		ctx->A[Q] >>= 1;
		ctx->B[Q] >>= 1;
	}

	ctx->N[Q]++; /* for next pixel */

	/* Do the actual Golomb encoding: */
	ctx->eor_limit = params->limit - ctx->limit_reduce;
	unary = MErrval >> k;
	if ( unary < ctx->eor_limit ) {
		put_zeros(ctx,bctx,unary);
		putbits(ctx,bctx,(1 << k) + (MErrval & ((1 << k) - 1)), k + 1);
	}
	else {
		put_zeros(ctx,bctx,ctx->eor_limit);
		putbits(ctx,bctx,(1<<params->qbpp) + MErrval-1, params->qbpp+1);
	}

	return 0;
}






/* For line and plane interleaved mode in LOSS-LESS mode */

int lossless_doscanline( const struct jls_enc_params *params,
			  struct jls_enc_ctx *ctx, struct bito_ctx *bctx,
			  const pixel *psl,            /* previous scanline */
			  const pixel *sl,             /* current scanline */
			  int no,                      /* number of values in it */
			  const int16_t classmap[])

/*** watch it! actual pixels in the scan line are numbered 1 to no .
     pixels with indices < 1 or > no are dummy "border" pixels  */
{
	int i;
	pixel Ra, Rb, Rc, Rd,   /* context pixels */
	      Ix,	            /* current pixel */
	      Px; 				/* predicted current pixel */

	int SIGN;			    /* sign of current context */
	int cont;				/* context */

	i = 1;    /* pixel indices in a scan line go from 1 to no */

	/**********************************************/
	/* Do for all pixels in the row in 8-bit mode */
	/**********************************************/
	Rc = psl[0];
	Rb = psl[1];
	Ra = sl[0];

	/*	For 8-bit Image */

	do {
		int RUNcnt;

		Ix = sl[i];
		Rd = psl[i + 1];

		/* Context determination */

		/* Quantize the gradient */
		/* partial context number: if (b-e) is used then its
			contribution is added after determination of the run state.
			Also, sign flipping, if any, occurs after run
			state determination */


		cont =  params->vLUT[Rd - Rb + ALPHA(params)][0] +
				params->vLUT[Rb - Rc + ALPHA(params)][1] +
				params->vLUT[Rc - Ra + ALPHA(params)][2];

		if ( cont == 0 )
		{
	/*************** RUN STATE ***************************/

			RUNcnt = 0;

			if (Ix == Ra) {
				while ( 1 ) {

					++RUNcnt;

					if (++i > no) {
						/* Run-length coding when reach end of line (A.7.1.2) */
						CHECK_RET(process_run(ctx, bctx,RUNcnt, EOLINE));
						return 0;	 /* end of line */
					}

					Ix = sl[i];

					if (Ix != Ra)	/* Run is broken */
					{
						Rd = psl[i + 1];
						Rb = psl[i];
						break;  /* out of while loop */
					}
					/* Run continues */
				}
			}

			/* we only get here if the run is broken by
				a non-matching symbol */

			/* Run-length coding when end of line not reached (A.7.1.2) */
			CHECK_RET(process_run(ctx, bctx,RUNcnt,NOEOLINE));


			/* This is the END_OF_RUN state */
			CHECK_RET(lossless_end_of_run(params, ctx, bctx, Ra, Rb, Ix, (Ra==Rb)));

		}
		else {

	/*************** REGULAR CONTEXT *******************/

			predict(Rb, Ra, Rc);

			/* do symmetric context merging */
			cont = classmap[cont];

			if (cont<0) {
				SIGN=-1;
				cont = -cont;
			}
			else
				SIGN=+1;

			/* output a rice code */
			CHECK_RET(lossless_regular_mode(params, ctx, bctx, cont, SIGN, Px, Ix));
		}

		/* context for next pixel: */
		Ra = Ix;
		Rc = Rb;
		Rb = Rd;
	} while (++i <= no);

	return 0;
}

