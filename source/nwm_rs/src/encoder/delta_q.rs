// Contains code modified from github.com/libjpeg-turbo/libjpeg-turbo for use in NTR-HR
// See LICENSE-libjpeg-turbo.md at the project root for license details

use super::*;

pub const DELTA_Q_COUNT: u8 = 32;
pub const DELTA_Q_MAX: f32 = 7f32;

pub const DELTA_Q_STEP: f32 = DELTA_Q_MAX / DELTA_Q_COUNT as f32;
pub const MIN_DCT_COMP_SIZE: usize = 9;

pub const SCALE_QD_F: f32 = (DELTA_Q_COUNT - 1) as f32;
pub const SCALE_QD_I_F: f32 = 1f32 / SCALE_QD_F;

#[derive(ConstDefault)]
pub struct DeltaQCoefs {
    pub m: f32,
    pub p: f32,
    pub d: f32,
}

#[derive(ConstDefault)]
pub struct DeltaQManager {
    pub f: [DeltaQCoefs; RP_DELTA_Q_COEFS_COUNT as usize],
    pub qb: f32,
    pub qc: f32,
    pub q: f32,
    pub nbits: f32,
    pub qd: u8,
    pub n: u8,
    pub d: u8,
}

#[derive(ConstDefault, Clone, Copy)]
pub struct QuantizeCounts {
    pub nbits: u16,
}

#[derive(ConstDefault)]
pub struct QuantizeRet {
    pub dc: QuantizeCounts,
    pub ac: QuantizeCounts,
}

pub struct DeltaQParams {
    pub qf: [f32; NUM_QUANT_TBLS],
    pub q_steps_i: f32,
    pub m: f32,
}

pub const DELTA_Q_CACHE_COUNTS: [u8; MAX_COMPONENTS] = [10, 5, 5];
pub const DELTA_Q_CACHE_MAX: u8 = {
    let mut max = 0;
    let mut i = 0;
    loop {
        if i >= MAX_COMPONENTS {
            break;
        }
        if max < DELTA_Q_CACHE_COUNTS[i] {
            max = DELTA_Q_CACHE_COUNTS[i];
        }
        i += 1;
    }
    max
};
pub const DELTA_Q_CACHE_TOTAL: u8 = {
    let mut total = 0;
    let mut i = 0;
    loop {
        if i >= MAX_COMPONENTS {
            break;
        }
        total += DELTA_Q_CACHE_COUNTS[i];
        i += 1;
    }
    total
};

pub struct DeltaQCache {
    pub cache: JBlock,
    pub next: JBlock,
    pub xpos: u16,
    pub ypos: u16,
}

fn dq_cache_gen_unique_indices(
    rand32: &mut Rand32,
    indices: &mut [u8; DELTA_Q_CACHE_MAX as usize],
    n: u8,
    m: u8,
) {
    for i in 0..n {
        let mut v = rand32.rand_range(0..(m - i) as u32) as u8;
        for j in 0..i {
            if v >= indices[j as usize] {
                v += 1;
            }
        }
        let mut done = false;
        for j in 0..i {
            if v < indices[j as usize] {
                for k in (j..i).rev() {
                    indices[k as usize + 1] = indices[k as usize];
                }
                indices[j as usize] = v;
                done = true;
                break;
            }
        }
        if !done {
            indices[i as usize] = v;
        }
    }
}

impl<'a, 'b> JpegEncode<'a, 'b> {
    pub fn compute_dq(&mut self, prev: *mut JBlock) {
        let need_ov_stats = unsafe { (*config_consts::NTR_CONFIG).ex.plg.overlayStats > 0 };

        let s = is_top_index(self.worker.data.info.is_top);
        let screen = s.index_into(&self.worker.data.shared.screens);
        let jpeg_screen = s.index_into(&self.worker.jpeg_shared.screens);
        let w = self.worker.data.info.work_index;
        let hss = screen.max_h_samp_factor == SAMP_FACTOR;
        let vss = screen.max_v_samp_factor == SAMP_FACTOR;

        let shared_mut = unsafe { &mut *self.worker.jpeg_shared_mut };

        let delta_q = unsafe {
            shared_mut
                .delta_q
                .get_mut(&s)
                .get_unchecked_mut(self.worker.data.info.even_odd as usize)
        };
        let rand32 = unsafe { &mut (*self.worker.shared_mut).rand32 };
        let cache = shared_mut.delta_q_cache.get_mut(&w);
        let cache_next_i = shared_mut.delta_q_cache_next.get_mut(&w);

        let prev_delta_q = *delta_q;
        let delta_q0 = &self.worker.jpeg_shared.delta_q0_tbls[prev_delta_q as usize];
        let div_parts = &jpeg_screen.divisors.divisors;
        let div_shifts = &self.worker.jpeg_shared.div_delta_q_shifts[DELTA_Q_COUNT as usize - 1];

        let mut delta_cache_start = 0;
        let mut blkn_start = 0;

        let mut qnv: [QuantizeRet; NUM_QUANT_TBLS] = const_default();
        let mut qnc: [u8; NUM_QUANT_TBLS] = const_default();

        let _need_wait_for_nwm = self.worker.data.thread_index.get() == 0;

        for ci in CompIndex::all() {
            *cache_next_i.get_mut(&ci) = 0;

            let mut indices: [u8; DELTA_Q_CACHE_MAX as usize] = const_default();

            let comp = unsafe {
                ci.index_into(&(**s.index_into(&self.worker.data.shared.comp_infos)).infos)
            };
            let qni = comp.quant_tbl_no;
            let mcu_we = comp.h_samp_exp;
            let mcu_he = comp.v_samp_exp;

            let count = *ci.index_into(&DELTA_Q_CACHE_COUNTS);

            dq_cache_gen_unique_indices(rand32, &mut indices, count, unsafe {
                core::intrinsics::unchecked_shl(jpeg_screen.mcus_per_row as u8, mcu_we + mcu_he)
            });

            for qi in 0..count {
                let delta_cache_i = delta_cache_start + qi;
                let blkn = indices[qi as usize];

                let cache = unsafe { cache.get_unchecked_mut(delta_cache_i as usize) };

                let mcu_i = unsafe { core::intrinsics::unchecked_shr(blkn, mcu_we + mcu_he) };
                let mcu_r =
                    blkn & (unsafe { core::intrinsics::unchecked_shl(1, mcu_we + mcu_he) } - 1);

                let xpos = mcu_r & (unsafe { core::intrinsics::unchecked_shl(1, mcu_we) } - 1);
                let ypos = unsafe { core::intrinsics::unchecked_shr(mcu_r, mcu_we) };

                let xpos = xpos + unsafe { core::intrinsics::unchecked_shl(mcu_i, mcu_we) };
                let xpos = xpos as usize * DCTSIZE;
                let ypos = ypos as usize * DCTSIZE;

                cache.xpos = xpos as u16;
                cache.ypos = ypos as u16;

                let prev = unsafe {
                    prev.add(
                        mcu_i as usize * jpeg_screen.max_blocks_in_mcu
                            + mcu_r as usize
                            + blkn_start,
                    )
                };

                let qn = if prev_delta_q == DELTA_Q_COUNT - 1 {
                    unsafe {
                        forward_dct(
                            self.worker.data.shared.delta_prog,
                            false,
                            false,
                            false,
                            screen.downsample,
                            &self.worker.data.bufs.prep,
                            ci,
                            hss,
                            vss,
                            &mut cache.cache,
                            ypos as u16,
                            xpos as u16,
                            div_parts.get_unchecked(comp.quant_tbl_no as usize),
                            div_shifts.get_unchecked(comp.quant_tbl_no as usize),
                            prev,
                            delta_q0.get_unchecked(comp.quant_tbl_no as usize),
                            &mut cache.next,
                        )
                    }
                } else {
                    unsafe {
                        forward_dct(
                            self.worker.data.shared.delta_prog,
                            false,
                            true,
                            false,
                            screen.downsample,
                            &self.worker.data.bufs.prep,
                            ci,
                            hss,
                            vss,
                            &mut cache.cache,
                            ypos as u16,
                            xpos as u16,
                            div_parts.get_unchecked(comp.quant_tbl_no as usize),
                            div_shifts.get_unchecked(comp.quant_tbl_no as usize),
                            prev,
                            delta_q0.get_unchecked(comp.quant_tbl_no as usize),
                            &mut cache.next,
                        )
                    }
                };

                let qnv = &mut *unsafe { qnv.get_unchecked_mut(qni as usize) };
                let update_qnv = |qnv: &mut QuantizeCounts, qn: &QuantizeCounts| {
                    qnv.nbits += qn.nbits;
                };
                update_qnv(&mut qnv.dc, &qn.dc);
                update_qnv(&mut qnv.ac, &qn.ac);
            }

            *unsafe { qnc.get_unchecked_mut(qni as usize) } += count;
            delta_cache_start += count;
            blkn_start += unsafe { core::intrinsics::unchecked_shl(1, mcu_we + mcu_he) };
        }

        const TARGET_FRAME_RATE: u32 = 60;
        let s_1 = is_top_index(!self.worker.data.info.is_top);
        let screen_1 = s_1.index_into(&self.worker.jpeg_shared.screens);

        let frame_time = entries::work_thread::get_frame_time(s)
            .load(Ordering::Acquire)
            .max(SYSCLOCK_ARM11 / TARGET_FRAME_RATE);
        let frame_time_1 = entries::work_thread::get_frame_time(s_1)
            .load(Ordering::Acquire)
            .max(SYSCLOCK_ARM11 / TARGET_FRAME_RATE);

        let frame_rate_get_clamp_min = |frame_time: u32| {
            f32::min(
                TARGET_FRAME_RATE as f32,
                SYSCLOCK_ARM11 as f32 / frame_time as f32,
            )
        };
        let frame_rate_clamp_max = |frame_rate: f32, _s: ScreenIndex| frame_rate.max(1f32);

        let frame_rate = frame_rate_get_clamp_min(frame_time);
        let frame_rate = frame_rate_clamp_max(frame_rate, s);
        let frame_rate_1 = frame_rate_get_clamp_min(frame_time_1);
        let frame_rate_1 = frame_rate_clamp_max(frame_rate_1, s_1);

        let quality = *s.index_into(&self.worker.jpeg_shared.quality);
        let qr =
            (DELTA_Q_COUNT as u32 / 6 + DELTA_Q_COUNT as u32 * quality * quality / 12000) as u8;
        let (qc, qc_1) = if s.get() == RP_SCREEN_TOP as u32 {
            let [qc, qc_1] = &mut shared_mut.delta_q_calc;
            (qc, qc_1)
        } else {
            let [qc_1, qc] = &mut shared_mut.delta_q_calc;
            (qc, qc_1)
        };
        let (qc, qc_1) = unsafe {
            (
                qc.get_unchecked_mut(self.worker.data.info.even_odd as usize),
                qc_1.get_unchecked_mut(self.worker.data.info.even_odd as usize),
            )
        };

        let current_qos = entries::thread_nwm::rp_delta_q_qos() as f32;

        let mcus = jpeg_screen.mcus as f32;
        let mcus_1 = screen_1.mcus as f32;
        let frame_rate_f = 1f32 / (frame_rate * mcus + frame_rate_1 * mcus_1);

        let mcusi = 1f32 / mcus;
        // let mcusi_1 = 1f32 / mcus_1;
        let qos_adj = {
            const QOS_ADJ_B: f32 = u8::BITS as f32;
            const QOS_MIN_F: f32 = 1f32;
            const QOS_MAX_L_F: f32 = 0.8f32;
            const QOS_MAX_H_F: f32 = 0.64f32;
            QOS_ADJ_B * QOS_MIN_F
                + (QOS_MAX_L_F
                    + (QOS_MAX_H_F - QOS_MAX_L_F)
                        * unsafe {
                            core::intrinsics::unchecked_div(
                                current_qos,
                                entries::thread_nwm::rp_rel_stream_max_qos() as f32,
                            )
                        }
                    - QOS_MIN_F)
                    * prev_delta_q as f32
                    * (QOS_ADJ_B / (DELTA_Q_COUNT - 1) as f32)
        };
        let qos_b = current_qos * frame_rate_f * qos_adj;

        let comp_size = unsafe {
            (*self.worker.shared_mut)
                .compressed_size
                .get(&s)
                .get_unchecked(self.worker.data.info.even_odd as usize)
                .load(Ordering::Acquire)
        };
        let comp_size = {
            let size = comp_size & ((1 << entries::thread_nwm::JPEG_COMP_COUNT_SIZE_NBITS) - 1);
            let blkn = (comp_size >> entries::thread_nwm::JPEG_COMP_COUNT_SIZE_NBITS)
                & ((1 << entries::thread_nwm::JPEG_COMP_COUNT_BLKN_NBITS) - 1);
            if blkn > 0 {
                size as f32 * mcus * jpeg_screen.max_blocks_in_mcu as f32 / blkn as f32
            } else {
                0f32
            }
        };
        let comp_size = comp_size * u8::BITS as f32 * mcusi;

        let (qos, qos_c) = if comp_size > 0f32 {
            let qos_d = if qc.qb > comp_size {
                qc.qb - comp_size
            } else if qc.qc < comp_size {
                qc.qc - comp_size
            } else {
                0f32
            };

            let update_coefs = |qe: &mut DeltaQCoefs, rb: f32| {
                let ri = 1f32 / rb;
                let r = 1f32 - ri;

                if false && need_ov_stats && qc.qd > 0 {
                    let qd = qc.qd as f32;

                    let nd = qc.nbits - comp_size;
                    let pm = qd * qe.m;
                    let pm = pm - nd;
                    let pm = pm * pm;

                    let w = unsafe { sqrtf(qd / (DELTA_Q_COUNT - 1) as f32) };
                    let wri = w * ri;
                    let wr = 1f32 - wri;
                    qe.p = qe.p * wr + pm * wri;

                    if nd > 0f32 {
                        let m = nd / qd;
                        qe.m = qe.m * wr + m * wri;
                    }
                }
                let qd_a = qe.d.abs();
                let qd_b = qos_d.abs();
                if qd_a * ri > qd_b || qd_b * ri > qd_a {
                    qe.d = qos_d;
                } else {
                    qe.d = qe.d * r + qos_d * ri;
                }
            };

            let rr: [f32; RP_DELTA_Q_COEFS_COUNT as usize] = [2f32];
            for i in 0..(if need_ov_stats {
                RP_DELTA_Q_COEFS_COUNT as usize
            } else {
                1
            }) {
                update_coefs(&mut qc.f[i], rr[i]);
            }

            let qd = qc.f[0].d.min(0f32);
            let qd_1 = qc_1.f[0].d.max(0f32);
            let qos_c =
                qos_b + qd + qd_1 * frame_rate_1.min(frame_rate) * mcus_1 * mcusi / frame_rate;

            (qos_c, qos_c)
        } else {
            (qos_b, qos_b)
        };

        let nbits = {
            let mut ret = 0f32;
            let qf = &jpeg_screen.delta_q_params.qf;
            for i in 0..NUM_QUANT_TBLS {
                ret += (qnv[i].dc.nbits as f32 / qnc[i] as f32
                    + qnv[i].ac.nbits as f32 / qnc[i] as f32)
                    * qf[i];
            }
            ret + jpeg_screen.delta_q_params.m // todo
        };

        let (qd_1, qd_2) = {
            let q_steps_i = jpeg_screen.delta_q_params.q_steps_i;
            let comp_d = if comp_size > 0f32 {
                qos - comp_size
            } else {
                0f32
            };
            let qd_1 = comp_d * q_steps_i;
            let nd = qc.nbits - nbits;
            let qd_2 = nd * q_steps_i;

            // clamp small values, but not too small for the powf in next step
            let qd2_thres_f: f32 = 8f32 * 400f32 * mcusi;
            let qd2_thres: f32 = qd2_thres_f;
            let qd_2 = if qd_2 < 0f32 {
                (qd_2 + qd2_thres).min(0f32)
            } else {
                (qd_2 - qd2_thres).max(0f32)
            };

            let scale_qd = |qd: f32, np: f32, pp: f32, ns: f32, ps: f32| {
                if qd < 0f32 {
                    (-unsafe { powf(-qd, np) } * (SCALE_QD_F * ns)).max(-SCALE_QD_F)
                } else {
                    (unsafe { powf(qd, pp) } * (SCALE_QD_F * ps)).min(SCALE_QD_F)
                }
            };

            // scaled so smaller values are even less significant,
            // then give extra boost to the predicted value (likely wrong, TODO maybe)
            const P_1: f32 = core::f32::consts::SQRT_2;
            const P_2: f32 = core::f32::consts::FRAC_1_SQRT_2;
            let qd1 = scale_qd(qd_1, P_1, P_1, 1f32, 1f32);
            let qd2 = scale_qd(qd_2, P_2, P_2, 1f32, 1f32);

            let qd = qd1 + qd2;
            qc.q = qc.q * 0.5f32 + qd * 0.5f32;
            if qc.q > 0f32 && qd < 0f32 || qc.q < 0f32 && qd > 0f32 {
                qc.q = 0f32;
                qc.n = 1;
            } else if qd.abs() > 0.5f32 {
                qc.n = cmp::max(qc.n, 1);
                qc.d = cmp::max(qc.d, 1);

                let q_thres =
                    current_qos * (qd2_thres_f / RP_QOS_MAX as f32) * qc.d as f32 / qc.n as f32;
                qc.n *= 2;

                if qc.q.abs() >= q_thres {
                    let q = (prev_delta_q as i32 + unsafe { roundf(qd) } as i32).clamp(0, qr as i32)
                        as u8;
                    qc.d = cmp::max((*delta_q as i32 - q as i32).abs() as u8, 1);
                    *delta_q = q;
                    qc.qd = DELTA_Q_COUNT - 1 - q;
                    qc.nbits = nbits;
                    qc.q = 0f32;
                    qc.n = 1;
                }
            }

            (qd1, qd2)
        };

        qc.qb = qos_b;
        qc.qc = qos_c;
        if need_ov_stats {
            let ov_screen = unsafe {
                (*config_consts::OV_STATS)
                    .s
                    .get_unchecked_mut(s.get() as usize)
            };
            ov_screen.comp_size = (comp_size * 1000f32) as s32;
            let ov_screen = &mut ov_screen.delta_q;
            // ov_screen.qb = (qos_b * 1000f32) as s32;
            // ov_screen.qc = (qos_c * 1000f32) as s32;
            ov_screen.qb = (qd_1 * 1000f32) as s32;
            ov_screen.qc = (qd_2 * 1000f32) as s32;
            ov_screen.nbits = (nbits * 1000f32) as s32;
            ov_screen.qd = qc.qd as u32;
            for i in 0..RP_DELTA_Q_COEFS_COUNT as usize {
                let f = &mut ov_screen.f[i];
                f.d = (qc.f[i].d * 1000f32) as s32;
            }
        }

        let d_qrescale_prev = *delta_q as i8 - prev_delta_q as i8;
        *shared_mut.work_delta_q.get_mut(&w) = *delta_q;
        *shared_mut.dq_rescale_prev.get_mut(&w) = d_qrescale_prev;

        if d_qrescale_prev != 0 {
            for n in 0..NUM_QUANT_TBLS {
                let d_qshifts = unsafe {
                    &self
                        .worker
                        .jpeg_shared
                        .delta_q_tbls
                        .get_unchecked(*delta_q as usize)
                        .get_unchecked(n)
                };

                let dq_shifts_prev = unsafe {
                    &self
                        .worker
                        .jpeg_shared
                        .delta_q_tbls
                        .get_unchecked(prev_delta_q as usize)
                        .get_unchecked(n)
                };

                let rp_shifts = unsafe { shared_mut.rp_shifts.get_mut(&w).get_unchecked_mut(n) };

                for i in 0..DCTSIZE2 {
                    rp_shifts[i] = if d_qrescale_prev > 0 {
                        dq_shifts_prev[i] - d_qshifts[i]
                    } else {
                        d_qshifts[i] - dq_shifts_prev[i]
                    };
                }
            }
        }
    }
}
