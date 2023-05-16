/*
 * JPEG-LS encoder
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
 * JPEG-LS encoder.
 */

#define UNCHECKED_BITSTREAM_READER 1
#include "mathops.h"
#include "jpegls.h"

/**
 * Encode error from regular symbol
 */
static inline void ls_encode_regular(JLSState *state, PutBitContext *pb, int Q,
                                     int err)
{
    int k;
    int val;
    int map;

    for (k = 0; (state->N[Q] << k) < state->A[Q]; k++)
        ;

    map = !NEAR && !k && (2 * state->B[Q] <= -state->N[Q]);

    if (err < 0)
        err += state->range;
    if (err >= (state->range + 1 >> 1)) {
        err -= state->range;
        val  = 2 * FFABS(err) - 1 - map;
    } else
        val = 2 * err + map;

    set_ur_golomb_jpegls(pb, val, k, state->limit, state->qbpp);

    ff_jpegls_update_state_regular(state, Q, err);
}

/**
 * Encode error from run termination
 */
static inline void ls_encode_runterm(JLSState *state, PutBitContext *pb,
                                     int RItype, int err, int limit_add)
{
    int k;
    int val, map;
    int Q = 365 + RItype;
    int temp;

    temp = state->A[Q];
    if (RItype)
        temp += state->N[Q] >> 1;
    for (k = 0; (state->N[Q] << k) < temp; k++)
        ;
    map = 0;
    if (!k && err && (2 * state->B[Q] < state->N[Q]))
        map = 1;

    if (err < 0)
        val = -(2 * err) - 1 - RItype + map;
    else
        val = 2 * err - RItype - map;
    set_ur_golomb_jpegls(pb, val, k, state->limit - limit_add - 1, state->qbpp);

    if (err < 0)
        state->B[Q]++;
    state->A[Q] += (val + 1 - RItype) >> 1;

    ff_jpegls_downscale_state(state, Q);
}

/**
 * Encode run value as specified by JPEG-LS standard
 */
static inline void ls_encode_run(JLSState *state, PutBitContext *pb, int run,
                                 int comp, int trail)
{
    while (run >= (1 << ff_log2_run[state->run_index[comp]])) {
        put_bits(pb, 1, 1);
        run -= 1 << ff_log2_run[state->run_index[comp]];
        if (state->run_index[comp] < 31)
            state->run_index[comp]++;
    }
    /* if hit EOL, encode another full run, else encode aborted run */
    if (!trail && run) {
        put_bits(pb, 1, 1);
    } else if (trail) {
        put_bits(pb, 1, 0);
        if (ff_log2_run[state->run_index[comp]])
            put_bits(pb, ff_log2_run[state->run_index[comp]], run);
    }
}

/**
 * Encode one line of image
 */
void ls_encode_line(JLSState *state, PutBitContext *pb,
                    const uint8_t *last, const uint8_t *in, int last2, int w)
{
    const int stride = 1, comp = 0, bits = 8;
    int x = 0;
    int Ra = R(last, 0), Rb, Rc = last2, Rd;
    int D0, D1, D2;

    while (x < w) {
        int err, pred, sign;

        /* compute gradients */
        Rb = R(last, x);
        Rd = (x >= w - stride) ? R(last, x) : R(last, x + stride);
        D0 = Rd - Rb;
        D1 = Rb - Rc;
        D2 = Rc - Ra;

        /* run mode */
        if ((FFABS(D0) <= NEAR) &&
            (FFABS(D1) <= NEAR) &&
            (FFABS(D2) <= NEAR)) {
            int RUNval, RItype, run;

            run    = 0;
            RUNval = Ra;
            while (x < w && (FFABS(R(in, x) - RUNval) <= NEAR)) {
                run++;
                W(last, x, Ra);
                x += stride;
            }
            ls_encode_run(state, pb, run, comp, x < w);
            if (x >= w)
                return;
            Rb     = R(last, x);
            RItype = FFABS(Ra - Rb) <= NEAR;
            pred   = RItype ? Ra : Rb;
            err    = R(in, x) - pred;

            if (!RItype && Ra > Rb)
                err = -err;

            if (NEAR) {
                if (err > 0)
                    err =  (NEAR + err) / TWO_NEAR;
                else
                    err = -(NEAR - err) / TWO_NEAR;

                if (RItype || (Rb >= Ra))
                    Ra = av_clip(pred + err * TWO_NEAR, 0, state->maxval);
                else
                    Ra = av_clip(pred - err * TWO_NEAR, 0, state->maxval);
            } else
                Ra = R(in, x);
            W(last, x, Ra);

            if (err < 0)
                err += state->range;
            if (err >= state->range + 1 >> 1)
                err -= state->range;

            ls_encode_runterm(state, pb, RItype, err,
                              ff_log2_run[state->run_index[comp]]);

            if (state->run_index[comp] > 0)
                state->run_index[comp]--;
        } else { /* regular mode */
            int context;

            context = ff_jpegls_quantize(state, D0) * 81 +
                      ff_jpegls_quantize(state, D1) *  9 +
                      ff_jpegls_quantize(state, D2);
            pred    = mid_pred(Ra, Ra + Rb - Rc, Rb);

            if (context < 0) {
                context = -context;
                sign    = 1;
                pred    = av_clip(pred - state->C[context], 0, state->maxval);
                err     = pred - R(in, x);
            } else {
                sign = 0;
                pred = av_clip(pred + state->C[context], 0, state->maxval);
                err  = R(in, x) - pred;
            }

            if (NEAR) {
                if (err > 0)
                    err =  (NEAR + err) / TWO_NEAR;
                else
                    err = -(NEAR - err) / TWO_NEAR;
                if (!sign)
                    Ra = av_clip(pred + err * TWO_NEAR, 0, state->maxval);
                else
                    Ra = av_clip(pred - err * TWO_NEAR, 0, state->maxval);
            } else
                Ra = R(in, x);
            W(last, x, Ra);

            ls_encode_regular(state, pb, context, err);
        }
        Rc = Rb;
        x += stride;
    }
}
