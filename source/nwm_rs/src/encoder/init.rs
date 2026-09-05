// Contains code modified from github.com/libjpeg-turbo/libjpeg-turbo for use in NTR-HR
// See LICENSE-libjpeg-turbo.md at the project root for license details

use super::*;

#[cfg(not(feature = "o3ds"))]
impl JpegSharedMut {
    fn init(&mut self, delta_prog: bool, params: [(usize, f32); RP_SCREEN_COUNT as usize]) {
        unsafe {
            ptr::write_bytes(self as *mut _ as *mut u8, 0, mem::size_of_val(self));
        }

        if delta_prog {
            for s in ScreenIndex::all() {
                for i in 0..DOWNSAMPLE_FACTOR {
                    (*self.delta_q.get_mut(&s))[i] = DELTA_Q_COUNT - 1;
                }
            }
        }

        self.delta_q_calc = const_default();

        if delta_prog {
            for i in 0..SCREEN_COUNT as usize {
                let (max_blocks_in_mcu, q_steps) = params[i];

                for k in 0..DOWNSAMPLE_FACTOR {
                    for j in 0..RP_DELTA_Q_COEFS_COUNT as usize {
                        self.delta_q_calc[i][k].f[j].m = q_steps;
                        self.delta_q_calc[i][k].f[j].p = q_steps * q_steps;
                    }
                    self.delta_q_calc[i][k].nbits = (MIN_DCT_COMP_SIZE * max_blocks_in_mcu) as f32;
                }
            }
        }
    }
}

impl EncoderShared {
    #[named]
    #[allow(unused_macros)]
    fn init(
        &mut self,
        downsample: [u32; RP_SCREEN_COUNT as usize],
        #[cfg(not(feature = "o3ds"))] rel_stream: bool,
        #[cfg(not(feature = "o3ds"))] delta_prog: bool,
        #[cfg(not(feature = "o3ds"))] core_count: CoreCount,
    ) -> Option<()> {
        #[cfg(not(feature = "o3ds"))]
        {
            self.rel_stream = rel_stream;
            self.delta_prog = delta_prog;
            self.core_count = core_count;

            for w in WorkIndex::all() {
                let sem = self.work_sem.get_mut(&w);
                if *sem > 0 {
                    unsafe {
                        let _ = svcCloseHandle(*sem);
                    }
                    *sem = 0;
                }

                let res = unsafe { svcCreateSemaphore(sem, 0, core_count.get() as i32 - 1) };
                if res != 0 {
                    ns_dbg_print!(create_semaphore_failed, c_str!("jpeg work_sem"), res);
                    return None;
                }
            }
            for s in ScreenIndex::all() {
                let sem = self.screen_sem.get_mut(&s);
                if *sem > 0 {
                    unsafe {
                        let _ = svcCloseHandle(*sem);
                    }
                    *sem = 0;
                }

                let res = unsafe { svcCreateSemaphore(sem, 1, 1) };
                if res != 0 {
                    ns_dbg_print!(create_semaphore_failed, c_str!("jpeg screen_sem"), res);
                    return None;
                }
            }
        }
        #[cfg(feature = "o3ds")]
        let delta_prog = false;

        self.last_restart_range = if delta_prog { 64 } else { 32 };

        for s in ScreenIndex::all() {
            let screen = s.index_into_mut(&mut self.screens);
            let is_top = s.get() == RP_SCREEN_TOP as u32;

            #[cfg(not(feature = "mem3"))]
            {
                screen.downsample = *s.index_into(&downsample) as u8;

                if screen.downsample == RP_DOWNSAMPLE_CHECKER {
                    screen.width = downsample_checker_screen_dim(is_top) as u16;
                    screen.height = screen.width;
                } else {
                    screen.width = downsample_screen_width(screen.downsample) as u16;
                    screen.height = downsample_screen_height(screen.downsample, is_top) as u16;
                }
            }
            #[cfg(feature = "mem3")]
            {
                screen.downsample = *s.index_into(&downsample) as u8;
                screen.width = downsample_screen_width(screen.downsample) as u16;
                screen.height = downsample_screen_height(screen.downsample, is_top) as u16;
            }

            screen.max_h_samp_factor = 1;
            screen.max_v_samp_factor = 1;
        }

        Some(())
    }
}

impl LosslessShared {
    fn init(&mut self, color_bias: [u8; RP_SCREEN_COUNT as usize]) {
        for s in ScreenIndex::all() {
            let bias = s.index_into_mut(&mut self.color_bias);
            let color_bias = *s.index_into(&color_bias);
            *bias = color_bias;
        }
    }
}

impl JpegShared {
    #[named]
    pub fn init(
        &mut self,
        quality: [u32; RP_SCREEN_COUNT as usize],
        hq: [u32; RP_SCREEN_COUNT as usize],
        shared: &mut EncoderShared,
    ) -> [(usize, f32); RP_SCREEN_COUNT as usize] {
        #[cfg(not(feature = "o3ds"))]
        let delta_prog = shared.delta_prog;
        #[cfg(feature = "o3ds")]
        let delta_prog = false;

        unsafe {
            if jpeg_quality_update_acquire(self).is_none() {
                return const_default();
            }
        }

        self.quality = quality;
        for s in ScreenIndex::all() {
            let screen = s.index_into_mut(&mut self.screens);
            let quality = *s.index_into(&quality);

            if !delta_prog || s.get() == RP_SCREEN_TOP as u32 {
                screen
                    .quant_tbls
                    .set_quality(if delta_prog { 100 } else { quality });
                screen
                    .divisors
                    .set_divisors(&screen.quant_tbls, &mut screen.div_shifts);
            }
        }

        #[cfg(not(feature = "o3ds"))]
        if delta_prog {
            for s in 1..RP_SCREEN_COUNT as usize {
                self.screens[s].quant_tbls = self.screens[RP_SCREEN_TOP as usize].quant_tbls;
                self.screens[s].divisors = self.screens[RP_SCREEN_TOP as usize].divisors;
                self.screens[s].div_shifts = self.screens[RP_SCREEN_TOP as usize].div_shifts;
            }

            for q in 0..DELTA_Q_COUNT {
                let div_shifts = &mut self.div_delta_q_shifts[q as usize];
                for i in 0..NUM_QUANT_TBLS {
                    let base_shifts = &self.screens[RP_SCREEN_TOP as usize].div_shifts[i];
                    let shifts = &mut div_shifts[i];
                    let ltbl = &self.delta_q_tbls[q as usize][i];

                    for i in 0..DCTSIZE2 {
                        shifts[i] = base_shifts[i] + ltbl[i];
                    }
                }
            }
        }

        let ret = self.set_comp_infos(
            hq,
            #[cfg(not(feature = "o3ds"))]
            delta_prog,
            shared,
        );

        unsafe {
            if jpeg_quality_update_release(self).is_none() {
                return const_default();
            }
        }

        ret
    }

    #[cfg(not(feature = "o3ds"))]
    fn once_delta_q_tbls(&mut self) {
        for d in (0..DELTA_Q_COUNT as usize).rev() {
            let f = DELTA_Q_MAX / DELTA_Q_COUNT as f32 * d as f32;

            for j in 0..NUM_QUANT_TBLS {
                let btbls = if j == 0 {
                    &STD_LUMINANCE_QUANT_TBL
                } else {
                    &STD_CHROMINANCE_QUANT_TBL
                };

                let mut log2_tbls: [f32; DCTSIZE2] = const_default();
                for i in 0..DCTSIZE2 {
                    let v = unsafe { log2f(btbls[i] as f32) };
                    log2_tbls[i] = v;
                }
                for i in 0..DCTSIZE2 {
                    let v = unsafe { roundf(f32::max(log2_tbls[i] - f, 0.0f32)) } as u8;
                    self.delta_q_tbls[d][j][i] = v;
                    let m = v - self.delta_q_tbls[DELTA_Q_COUNT as usize - 1][j][i];
                    self.delta_q0_tbls[d][j][i] = m;
                }
            }
        }
    }

    #[named]
    fn once(&mut self) {
        #[cfg(not(feature = "o3ds"))]
        self.once_delta_q_tbls();
    }

    fn set_comp_infos(
        &mut self,
        hq: [u32; RP_SCREEN_COUNT as usize],
        #[cfg(not(feature = "o3ds"))] delta_prog: bool,
        shared: &mut EncoderShared,
    ) -> [(usize, f32); RP_SCREEN_COUNT as usize] {
        let mut ret: [(usize, f32); RP_SCREEN_COUNT as usize] = const_default();

        for s in ScreenIndex::all() {
            #[cfg(not(feature = "mem3"))]
            let is_top = s.get() == RP_SCREEN_TOP as u32;
            let jpeg_screen = s.index_into_mut(&mut self.screens);
            let screen = s.index_into_mut(&mut shared.screens);
            let hq = *s.index_into(&hq) as u8;

            #[cfg(not(feature = "mem3"))]
            {
                jpeg_screen.checker = const_default();
            }

            let comp_infos = if hq == RP_CHROMASS_444 {
                &shared.encode_tbls.comp_infos_444
            } else if hq == RP_CHROMASS_422 {
                &shared.encode_tbls.comp_infos_422
            } else {
                &shared.encode_tbls.comp_infos_420
            };
            *s.index_into_mut(&mut shared.comp_infos) = comp_infos;
            jpeg_screen.max_blocks_in_mcu = 0;
            for i in 0..MAX_COMPONENTS {
                let info = &comp_infos.infos[i];
                screen.max_h_samp_factor =
                    cmp::max(screen.max_h_samp_factor, info.h_samp_factor as usize);
                screen.max_v_samp_factor =
                    cmp::max(screen.max_v_samp_factor, info.v_samp_factor as usize);
                jpeg_screen.max_blocks_in_mcu +=
                    info.h_samp_factor as usize * info.v_samp_factor as usize;
            }
            if jpeg_screen.max_blocks_in_mcu > MAX_BLOCKS_IN_MCU {
                panic!();
            }
            jpeg_screen.mcu_row_size = DCTSIZE * screen.max_h_samp_factor;
            jpeg_screen.mcu_col_size = DCTSIZE * screen.max_v_samp_factor;
            jpeg_screen.mcus_per_row =
                jdiv_round_up(screen.width as usize, jpeg_screen.mcu_row_size);
            jpeg_screen.mcu_rows =
                jdiv_round_up(screen.height as usize, jpeg_screen.mcu_col_size) as u16;
            jpeg_screen.mcus = jpeg_screen.mcus_per_row as u16 * jpeg_screen.mcu_rows;

            #[cfg(not(feature = "mem3"))]
            if screen.downsample == RP_DOWNSAMPLE_CHECKER {
                let tl = GSP_SCREEN_WIDTH;
                let br = if is_top {
                    GSP_SCREEN_HEIGHT_TOP
                } else {
                    GSP_SCREEN_HEIGHT_BOTTOM
                };

                let mcu_l_v = tl / jpeg_screen.mcu_col_size as u32;
                let mcu_l_r = tl % jpeg_screen.mcu_col_size as u32;
                let mcu_l_w = (mcu_l_r > 0) as u32;

                let mcu_r_v = br / jpeg_screen.mcu_col_size as u32;
                let mcu_r_r = br % jpeg_screen.mcu_col_size as u32;
                let mcu_r_w = (mcu_r_r > 0) as u32;

                let checker = &mut jpeg_screen.checker;

                let mut mcus = 0;
                for mcu_y_start in 0..jpeg_screen.mcu_rows as u32 {
                    let params = &mut checker.mcu_row_params[mcu_y_start as usize];

                    let x_start = if mcu_y_start < mcu_l_v {
                        let y_end = (mcu_y_start + 1) * jpeg_screen.mcu_col_size as u32;
                        tl - y_end
                    } else if mcu_y_start < mcu_l_v + mcu_l_w {
                        0
                    } else {
                        let y_start =
                            (mcu_y_start - mcu_l_v) * jpeg_screen.mcu_col_size as u32 - mcu_l_r;
                        y_start
                    };
                    params.mcu_col_start = (x_start / jpeg_screen.mcu_row_size as u32) as u16;

                    let x_end = if mcu_y_start < mcu_r_v {
                        let y_end = (mcu_y_start + 1) * jpeg_screen.mcu_col_size as u32;
                        tl + y_end
                    } else if mcu_y_start < mcu_r_v + mcu_r_w {
                        tl + br
                    } else {
                        let y_start =
                            (mcu_y_start - mcu_r_v) * jpeg_screen.mcu_col_size as u32 - mcu_r_r;
                        tl + (br - y_start)
                    };
                    params.mcu_col_end =
                        jdiv_round_up(x_end as usize, jpeg_screen.mcu_row_size) as u16;

                    mcus += params.mcu_col_end - params.mcu_col_start;
                }

                checker.mcus = mcus;
                const MCU_STEP: u16 = 8;
                checker.mcus_per_row = MCU_STEP;
                checker.mcu_rows =
                    jdiv_round_up(checker.mcus as usize, checker.mcus_per_row as usize) as u16;
            }

            #[cfg(not(feature = "o3ds"))]
            {
                *s.index_into_mut(&mut ret) = if delta_prog {
                    let mut qf: [u16; NUM_QUANT_TBLS] = const_default();
                    for ci in 0..MAX_COMPONENTS {
                        let comp = &comp_infos.infos[ci];
                        let mcu_we = comp.h_samp_exp;
                        let mcu_he = comp.v_samp_exp;

                        qf[comp.quant_tbl_no as usize] += 1 << mcu_we + mcu_he;
                    }
                    const QF_F: [f32; NUM_QUANT_TBLS] = [1.25f32, 2f32 / 3f32];
                    let qf = {
                        let mut ret: [f32; NUM_QUANT_TBLS] = const_default();
                        for i in 0..NUM_QUANT_TBLS {
                            ret[i] = qf[i] as f32 * QF_F[i];
                        }
                        ret
                    };
                    jpeg_screen.delta_q_params.qf = qf;
                    let qt = {
                        let mut qt = 0f32;
                        for i in 0..NUM_QUANT_TBLS {
                            qt += jpeg_screen.delta_q_params.qf[i];
                        }
                        qt
                    };
                    let q_step = (DELTA_Q_STEP * qt, DELTA_Q_STEP * (DCTSIZE2 - 1) as f32 * qt);
                    let q_steps = q_step.0 + q_step.1;
                    let q_steps_i = 1f32 / q_steps;
                    jpeg_screen.delta_q_params.q_steps_i = q_steps_i * SCALE_QD_I_F;

                    jpeg_screen.delta_q_params.m =
                        (MIN_DCT_COMP_SIZE * jpeg_screen.max_blocks_in_mcu) as f32;

                    (jpeg_screen.max_blocks_in_mcu, q_steps)
                } else {
                    (0, 0f32)
                };
            }

            #[cfg(feature = "o3ds")]
            {
                *s.index_into_mut(&mut ret) = (0, 0f32);
            }
        }
        ret
    }
}

#[cfg(not(feature = "o3ds"))]
impl CommonSharedMut {
    pub fn once(&mut self) {
        self.rand32 = Rand32::new(get_system_tick().get() as u64);
    }

    pub fn init(&mut self, delta_prog: bool, core_count: CoreCount) {
        if delta_prog {
            self.compressed_size = const_default();
        }

        for w in WorkIndex::all() {
            self.work_inited.get_mut(&w).store(false, Ordering::Release);
            self.work_sem_count
                .get_mut(&w)
                .store(core_count.get() as u8, Ordering::Release);
        }
        for s in ScreenIndex::all() {
            self.screen_bool.get_mut(&s).store(false, Ordering::Release);
            *self.last_restart_interval.get_mut(&s) = 0;
        }
    }
}

impl Encoder {
    pub unsafe fn once(&mut self) {
        unsafe {
            ptr::write_bytes(self as *mut _ as *mut u8, 0, mem::size_of_val(self));
        }
        self.shared.encode_tbls = EncodeTbls::once();
        self.jpeg_shared.once();
        #[cfg(not(feature = "o3ds"))]
        self.common_shared_mut.once();
    }

    #[named]
    #[allow(unused_macros)]
    pub fn init(
        &mut self,
        quality: [u32; RP_SCREEN_COUNT as usize],
        #[cfg(not(feature = "o3ds"))] core_count: CoreCount,
        hq: [u32; RP_SCREEN_COUNT as usize],
        downsample: [u32; RP_SCREEN_COUNT as usize],
        color_bias: [u8; RP_SCREEN_COUNT as usize],
        #[cfg(not(feature = "o3ds"))] rel_stream: bool,
        #[cfg(not(feature = "o3ds"))] delta_prog: bool,
    ) -> Option<()> {
        self.lossless_shared.init(color_bias);
        self.shared.init(
            downsample,
            #[cfg(not(feature = "o3ds"))]
            rel_stream,
            #[cfg(not(feature = "o3ds"))]
            delta_prog,
            #[cfg(not(feature = "o3ds"))]
            core_count,
        )?;

        #[cfg(not(feature = "o3ds"))]
        self.common_shared_mut.init(delta_prog, core_count);

        #[cfg(not(feature = "o3ds"))]
        {
            let shared = &mut self.jpeg_shared;
            shared.quality_need_update.store(false, Ordering::Relaxed);
            unsafe {
                if shared.quality_can_update != 0 {
                    let _ = svcCloseHandle(shared.quality_can_update);
                    shared.quality_can_update = 0;
                }
                let res = svcCreateSemaphore(&mut shared.quality_can_update, 1, 1);
                if res != 0 {
                    ns_dbg_print!(
                        create_semaphore_failed,
                        c_str!("jpeg shared quality_can_update"),
                        res
                    );
                    return None;
                }

                if shared.quality_done_update != 0 {
                    let _ = svcCloseHandle(shared.quality_done_update);
                    shared.quality_done_update = 0;
                }
                let res = svcCreateEvent(&mut shared.quality_done_update, RESET_STICKY);
                if res != 0 {
                    ns_dbg_print!(
                        create_event_failed,
                        c_str!("jpeg shared quality_done_update"),
                        res
                    );
                    return None;
                }
            }
        }

        let shared_mut_params = self.jpeg_shared.init(quality, hq, &mut self.shared);
        #[cfg(feature = "o3ds")]
        {
            let _ = shared_mut_params;
        };
        #[cfg(not(feature = "o3ds"))]
        if entries::thread_nwm::get_lossless_compression() {
            let shared_mut = unsafe { &mut self.encoder_shared_mut.lossless };
            unsafe {
                ptr::write_bytes(
                    shared_mut as *mut _ as *mut u8,
                    0,
                    mem::size_of_val(shared_mut),
                );
            }
        } else {
            let shared_mut = unsafe { &mut self.encoder_shared_mut.jpeg };
            shared_mut.init(delta_prog, shared_mut_params);
        }

        Some(())
    }
}
