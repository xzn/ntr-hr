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

    map = !k && (2 * state->B[Q] <= -state->N[Q]);

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
                                 int trail)
{
    while (run >= (1 << ff_log2_run[state->run_index])) {
        put_bits(pb, 1, 1);
        run -= 1 << ff_log2_run[state->run_index];
        if (state->run_index < 31)
            state->run_index++;
    }
    /* if hit EOL, encode another full run, else encode aborted run */
    if (!trail && run) {
        put_bits(pb, 1, 1);
    } else if (trail) {
        put_bits(pb, 1, 0);
        if (ff_log2_run[state->run_index])
            put_bits(pb, ff_log2_run[state->run_index], run);
    }
}

/**
 * Encode one line of image
 */
void ls_encode_line(JLSState *state, PutBitContext *pb,
                    const uint8_t *last, const uint8_t *in, int w, const uint16_t (*vLUT)[3], const int16_t classmap[])
{
    int x = 0;
    int Ra = in[-1], Rb = last[0], Rc = last[-1], Rd = last[1];
    int cont;

    while (1) {
        int err, pred, sign;

        /* compute gradients */
        cont =  vLUT[Rd - Rb + state->range][0] +
                vLUT[Rb - Rc + state->range][1] +
                vLUT[Rc - Ra + state->range][2];

        /* run mode */
        if (cont == 0) {
            int RUNval, RItype, run;

            run    = 0;
            RUNval = Ra;
            while (x < w && (in[x] - RUNval == 0)) {
                run++;
                x++;
            }
            ls_encode_run(state, pb, run, x < w);
            if (x >= w)
                return;
            Rb     = last[x];
            RItype = Ra - Rb == 0;
            pred   = RItype ? Ra : Rb;
            err    = in[x] - pred;

            if (!RItype && Ra > Rb)
                err = -err;

            Ra = in[x];

            if (err < 0)
                err += state->range;
            if (err >= state->range + 1 >> 1)
                err -= state->range;

            ls_encode_runterm(state, pb, RItype, err,
                              ff_log2_run[state->run_index]);

            if (state->run_index > 0)
                state->run_index--;

            x++;
            if (x >= w)
                break;

            Rc = Rb;
            Rb = last[x];
            Rd = last[x + 1];
        } else { /* regular mode */
            cont = classmap[cont];
            pred    = mid_pred(Ra, Ra + Rb - Rc, Rb);

            if (cont < 0) {
                cont = -cont;
                sign    = 1;
                pred    = av_clip(pred - state->C[cont], 0, state->maxval);
                err     = pred - in[x];
            } else {
                sign = 0;
                pred = av_clip(pred + state->C[cont], 0, state->maxval);
                err  = in[x] - pred;
            }

            Ra = in[x];

            ls_encode_regular(state, pb, cont, err);

            x++;
            if (x >= w)
                break;

            Rc = Rb;
            Rb = Rd;
            Rd = last[x + 1];
        }
    }
}
