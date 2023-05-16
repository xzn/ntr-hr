/* PMG/JPEG-LS IMPLEMENTATION V.2.1
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

/* global.c --- support and portability routines: error handling, safe memory
 *                              management, etc.
 *
 * Initial code by Alex Jakulin,  Aug. 1995
 *
 * Modified and optimized: Gadiel Seroussi, October 1995 - ...
 *
 * Modified and added Restart marker and input tables by:
 * David Cheng-Hsiu Chu, and Ismail R. Ismail march 1999
 */

#include "global.h"



char *disclaimer =
"This program is Copyright (c) University of British Columbia.\n\
All rights reserved. It may be freely redistributed in its\n\
entirety provided that this copyright notice is not removed.\n\
It may not be sold for profit or incorporated in commercial programs\n\
without the written permission of the copyright holder.\n";


/* Set thresholds to default unless specified by header: */

void set_thresholds(int alfa, int *T1p, int *T2p, int *T3p)
{
	int lambda,
	    ilambda = 256/alfa,
	    quant = 2*NEAR+1,
	    T1 = *T1p,
	    T2 = *T2p,
	    T3 = *T3p;

	if (alfa<4096)
		lambda = (alfa+127)/256;
	else
		lambda = (4096+127)/256;



	if ( T1 <= 0 )  {
		/* compute lossless default */
		if ( lambda )
			T1 = lambda*(BASIC_T1 - 2) + 2;
		else {  /* alphabet < 8 bits */
			T1 = BASIC_T1/ilambda;
			if ( T1 < 2 ) T1 = 2;
		}
		/* adjust for lossy */
		T1 += 3*NEAR;

		/* check that the default threshold is in bounds */
		if ( T1 < NEAR+1 || T1 > (alfa-1) )
		     T1 = NEAR+1;         /* eliminates the threshold */
	}
	if ( T2 <= 0 )  {
		/* compute lossless default */
		if ( lambda )
			T2 = lambda*(BASIC_T2 - 3) + 3;
		else {
			T2 = BASIC_T2/ilambda;
			if ( T2 < 3 ) T2 = 3;
		}
		/* adjust for lossy */
		T2 += 5*NEAR;

		/* check that the default threshold is in bounds */
		if ( T2 < T1 || T2 > (alfa-1) )
		     T2 = T1;         /* eliminates the threshold */
	}
	if ( T3 <= 0 )  {
		/* compute lossless default */
		if ( lambda )
			T3 = lambda*(BASIC_T3 - 4) + 4;
		else {
			T3 = BASIC_T3/ilambda;
			if ( T3 < 4 ) T3 = 4;
		}
		/* adjust for lossy */
		T3 += 7*NEAR;

		/* check that the default threshold is in bounds */
		if ( T3 < T2 || T3 > (alfa-1) )
		     T3 = T2;         /* eliminates the threshold */
	}

	*T1p = T1;
	*T2p = T2;
	*T3p = T3;
}

