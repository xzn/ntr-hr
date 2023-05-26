/*
 * JPEG-LS decoder
 * Copyright (c) 2003 Michael Niedermayer
 * Copyright (c) 2006 Konstantin Shishkov
 *
 * This file is part of FFmpeg.
 *
 * FFmpeg is free software; you can redistribute it and/or
 * modify it under the terms of the GNU Lesser General Public
 * License as published by the Free Software Foundation; either
 * version 2.1 of the License, or (at your option) any later version.
 *
 * FFmpeg is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
 * Lesser General Public License for more details.
 *
 * You should have received a copy of the GNU Lesser General Public
 * License along with FFmpeg; if not, write to the Free Software
 * Foundation, Inc., 51 Franklin Street, Fifth Floor, Boston, MA 02110-1301 USA
 */

/**
 * @file
 * JPEG-LS decoder.
 */

#include "golomb.h"
#include "jpegls.h"

/**
 * Get context-dependent Golomb code, decode it and update context
 */
static inline int ls_get_code_regular(GetBitContext *gb, JLSState *state, int Q)
{
    int k, ret;

    for (k = 0; ((unsigned)state->N[Q] << k) < state->A[Q]; k++)
        ;

#ifdef JLS_BROKEN
    if (!show_bits_long(gb, 32))
        return -1;
#endif
    ret = get_ur_golomb_jpegls(gb, k, state->limit, state->qbpp);

    /* decode mapped error */
    if (ret & 1)
        ret = -(ret + 1 >> 1);
    else
        ret >>= 1;

    /* for NEAR=0, k=0 and 2*B[Q] <= - N[Q] mapping is reversed */
    if (!k && (2 * state->B[Q] <= -state->N[Q]))
        ret = -(ret + 1);

    ret = ff_jpegls_update_state_regular(state, Q, ret);

    return ret;
}

/**
 * Get Golomb code, decode it and update state for run termination
 */
static inline int ls_get_code_runterm(GetBitContext *gb, JLSState *state,
                                      int RItype, int limit_add)
{
    int k, ret, temp, map;
    int Q = 365 + RItype;

    temp = state->A[Q];
    if (RItype)
        temp += state->N[Q] >> 1;

    for (k = 0; ((unsigned)state->N[Q] << k) < temp; k++)
        ;

#ifdef JLS_BROKEN
    if (!show_bits_long(gb, 32))
        return -1;
#endif
    ret = get_ur_golomb_jpegls(gb, k, state->limit - limit_add - 1,
                               state->qbpp);
    if (ret < 0)
        return -0x10000;

    /* decode mapped error */
    map = 0;
    if (!k && (RItype || ret) && (2 * state->B[Q] < state->N[Q]))
        map = 1;
    ret += RItype + map;

    if (ret & 1) {
        ret = map - (ret + 1 >> 1);
        state->B[Q]++;
    } else {
        ret = ret >> 1;
    }

    if (FFABS(ret) > 0xFFFF)
        return -0x10000;
    /* update state */
    state->A[Q] += FFABS(ret) - RItype;
    ff_jpegls_downscale_state(state, Q);

    return ret;
}

/**
 * Decode one line of image
 */
int ls_decode_line(JLSState *state, GetBitContext *gb,
                   const uint8_t *last, uint8_t *dst, int last2, int w)
{
    int i, x = 0;
    int Ra, Rb, Rc, Rd;
    int D0, D1, D2;

    while (x < w) {
        int err, pred;

        if (get_bits_left(gb) <= 0)
            return AVERROR_INVALIDDATA;

        /* compute gradients */
        Ra = x ? dst[x - 1] : last[x];
        Rb = last[x];
        Rc = x ? last[x - 1] : last2;
        Rd = (x >= w - 1) ? last[x] : last[x + 1];
        D0 = Rd - Rb;
        D1 = Rb - Rc;
        D2 = Rc - Ra;
        /* run mode */
        if ((D0 == 0) &&
            (D1 == 0) &&
            (D2 == 0)) {
            int r;
            int RItype;

            /* decode full runs while available */
            while (get_bits1(gb)) {
                int r;
                r = 1 << ff_log2_run[state->run_index];
                if (x + r * 1 > w)
                    r = (w - x) / 1;
                for (i = 0; i < r; i++) {
                    dst[x] = Ra;
                    x += 1;
                }
                /* if EOL reached, we stop decoding */
                if (r != 1 << ff_log2_run[state->run_index])
                    return 0;
                if (state->run_index < 31)
                    state->run_index++;
                if (x + 1 > w)
                    return 0;
            }
            /* decode aborted run */
            r = ff_log2_run[state->run_index];
            if (r)
                r = get_bits(gb, r);
            if (x + r * 1 > w) {
                r = (w - x) / 1;
            }
            for (i = 0; i < r; i++) {
                dst[x] = Ra;
                x += 1;
            }

            if (x >= w) {
                av_log(NULL, AV_LOG_ERROR, "run overflow\n");
                av_assert0(x <= w);
                return AVERROR_INVALIDDATA;
            }

            /* decode run termination value */
            Rb     = last[x];
            RItype = (Ra - Rb == 0) ? 1 : 0;
            err    = ls_get_code_runterm(gb, state, RItype,
                                         ff_log2_run[state->run_index]);
            if (state->run_index)
                state->run_index--;

            if (Rb < Ra)
                pred = Rb - err;
            else
                pred = Rb + err;
        } else { /* regular mode */
            int context, sign;

            context = ff_jpegls_quantize(state, D0) * 81 +
                      ff_jpegls_quantize(state, D1) *  9 +
                      ff_jpegls_quantize(state, D2);
            pred    = mid_pred(Ra, Ra + Rb - Rc, Rb);

            if (context < 0) {
                context = -context;
                sign    = 1;
            } else {
                sign = 0;
            }

            if (sign) {
                pred = av_clip(pred - state->C[context], 0, state->maxval);
                err  = -ls_get_code_regular(gb, state, context);
            } else {
                pred = av_clip(pred + state->C[context], 0, state->maxval);
                err  = ls_get_code_regular(gb, state, context);
            }

            /* we have to do something more for near-lossless coding */
            pred += err;
        }

        pred &= state->maxval;
        dst[x] = pred;
        x += 1;
    }

    return 0;
}
