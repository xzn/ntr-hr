// Contains code modified from github.com/libjpeg-turbo/libjpeg-turbo for use in NTR-HR
// See LICENSE-libjpeg-turbo.md at the project root for license details

#![allow(unused_imports)]

mod color_convert;
mod compress;
mod encode_buffer;
mod forward_dct;
mod pre_process;
mod write;

use super::*;

pub use color_convert::*;
pub use compress::*;
pub use encode_buffer::*;
pub use forward_dct::*;
pub use pre_process::*;
pub use write::*;

pub const fn subsamp_constraint(h_samp: bool, v_samp: bool) {
    match (h_samp, v_samp) {
        (true, true) => (),
        (true, false) => (),
        (false, true) => panic!(),
        (false, false) => (),
    }
}

struct SubSampConst<const H_SAMP: bool, const V_SAMP: bool>;

impl<const H_SAMP: bool, const V_SAMP: bool> SubSampConst<H_SAMP, V_SAMP> {
    const ASSERT: () = subsamp_constraint(H_SAMP, V_SAMP);
}

pub struct JpegEncode<'a, 'b> {
    pub worker: &'b mut JpegWorker<'a>,
    pub dst: WorkerDst,
}

#[cfg(not(feature = "mem3"))]
fn get_bpp_for_format(c: ColorSpace) -> u8 {
    match c {
        ColorSpace::RGBA8 => 4,
        ColorSpace::RGB8 => 3,
        _ => 2,
    }
}

#[cfg(not(feature = "o3ds"))]
type EncodeRet = JpegEncodeRet;

#[cfg(feature = "o3ds")]
type EncodeRet = ();

impl<'a, 'b> JpegEncode<'a, 'b> {
    #[named]
    #[allow(unused_macros)]
    pub fn encode<F, G>(
        &mut self,
        #[cfg(not(feature = "mem3"))] src: &[u8],
        #[cfg(feature = "mem3")] src: *const u8,
        #[cfg(feature = "mem3")] pitch: u32,
        #[cfg(not(feature = "o3ds"))] mut pre_progress: F,
        mut progress: G,
    ) -> Option<EncodeRet>
    where
        F: FnMut(),
        G: FnMut(usize),
    {
        #[cfg(not(feature = "mem3"))]
        let bpp = get_bpp_for_format(self.worker.data.info.color_space);
        let is_top = self.worker.data.info.is_top;
        #[cfg(not(feature = "o3ds"))]
        let w = self.worker.data.info.work_index;
        let s = is_top_index(is_top);
        let screen = s.index_into(&self.worker.data.shared.screens);
        #[cfg(not(feature = "mem3"))]
        let jpeg_shared = self.worker.jpeg_shared;
        #[cfg(not(feature = "mem3"))]
        let jpeg_screen = s.index_into(&jpeg_shared.screens);
        #[cfg(not(feature = "mem3"))]
        let pitch = GSP_SCREEN_WIDTH as usize * bpp as usize;

        #[cfg(not(feature = "o3ds"))]
        if !self.worker.data.shared.rel_stream && self.worker.data.thread_index.get() == 0 {
            self.write_headers();
        }
        #[cfg(feature = "o3ds")]
        self.write_headers();

        self.reset_mcu();

        #[cfg(not(feature = "o3ds"))]
        let prev = unsafe {
            if self.worker.data.shared.delta_prog {
                if src.len() == 0 {
                    empty_work_acquire(self.worker.data.shared, w)?;
                }

                let shared_mut = &mut *self.worker.jpeg_shared_mut;

                let prev = if is_top {
                    shared_mut.dq_prev_coeffs_top.as_mut_ptr()
                } else {
                    shared_mut.dq_prev_coeffs_bot.as_mut_ptr()
                };

                let prev = match screen.downsample {
                    RP_DOWNSAMPLE_CHECKER | RP_DOWNSAMPLE_EVEN_ODD => {
                        let count = (if is_top {
                            shared_mut.dq_prev_coeffs_top.len()
                        } else {
                            shared_mut.dq_prev_coeffs_bot.len()
                        }) / 2;
                        prev.add(count * self.worker.data.info.even_odd as usize)
                    }
                    RP_DOWNSAMPLE_QUARTER | _ => prev,
                };

                (prev as *mut JBlock).add(
                    self.worker.data.info.restart_interval as usize
                        * jpeg_screen.max_blocks_in_mcu
                        * self.worker.data.thread_index.get() as usize,
                )
            } else {
                ptr::null_mut()
            }
        };

        let hss = screen.max_h_samp_factor == SAMP_FACTOR;
        let vss = screen.max_v_samp_factor == SAMP_FACTOR;

        #[cfg(not(feature = "mem3"))]
        let mut src_iter = src.chunks_exact(pitch).map(|x| x.as_ptr());
        #[cfg(not(feature = "mem3"))]
        let height = src.len() / pitch;

        #[cfg(feature = "mem3")]
        let height = if is_top {
            GSP_SCREEN_HEIGHT_TOP
        } else {
            GSP_SCREEN_HEIGHT_BOTTOM
        } as usize;
        #[cfg(feature = "mem3")]
        let src = unsafe { slice::from_raw_parts(src, pitch as usize * height) };
        #[cfg(feature = "mem3")]
        let mut src_iter = src.chunks_exact(pitch as usize).map(|x| x.as_ptr());

        #[cfg(not(feature = "mem3"))]
        let checker = if screen.downsample == RP_DOWNSAMPLE_CHECKER {
            let mcu_total_start =
                self.worker.data.info.restart_interval * self.worker.data.thread_index.get() as u16;
            let mcu_total_count = self.worker.data.info.restart_interval;

            let mut mcu_curr_start = 0;
            let mut mcu_curr_count = 0;

            'f: for mcu_y in 0..jpeg_screen.mcu_rows {
                let params = &jpeg_screen.checker.mcu_row_params[mcu_y as usize];
                let mcu_col_count = params.mcu_col_end - params.mcu_col_start;

                for mcu_i in 0..mcu_col_count {
                    if mcu_curr_start >= mcu_total_start {
                        let _mcu_x = params.mcu_col_start + mcu_i;

                        #[cfg(not(feature = "o3ds"))]
                        if mcu_curr_count == j_max_half_factor(mcu_total_count as usize) as u16 {
                            pre_progress();
                        }

                        mcu_curr_count += 1;

                        if mcu_curr_count >= mcu_total_count {
                            break 'f;
                        }
                    } else {
                        mcu_curr_start += 1;
                    }
                }
            }
            true
        } else {
            false
        };

        #[cfg(feature = "mem3")]
        let checker = false;

        if !checker {
            let mut process_rem = |f: usize, proc: fn(*mut WorkerCommon<'a>, _) -> ()| {
                let f = DCTSIZE * f;
                let n = height / f;
                let rem = height % f;
                #[allow(unused)]
                for i in 0..n {
                    self.process(
                        #[cfg(not(feature = "o3ds"))]
                        prev,
                        #[cfg(not(feature = "o3ds"))]
                        i,
                        |this| {
                            /* Pre-process */
                            proc(&mut this.worker.data, ptr::from_mut(&mut src_iter));
                            #[cfg(not(feature = "o3ds"))]
                            if i == j_max_half_factor(n) {
                                pre_progress();
                            }
                            true
                        },
                        || {},
                    );
                    progress(f);
                }
                (rem, n)
            };
            let mut process = |f: usize, proc: fn(_, _) -> ()| {
                process_rem(f, proc);
            };

            match screen.downsample {
                RP_DOWNSAMPLE_QUARTER => {
                    if vss {
                        // vss == true
                        #[allow(unused)]
                        let (rem, n) =
                            process_rem(SAMP_FACTOR * DOWNSAMPLE_FACTOR, |f, i| unsafe {
                                (&mut *f).pre_process_quarter::<DCTSIZE>(&mut *i, 0)
                            });

                        if rem > 0 {
                            self.process(
                                #[cfg(not(feature = "o3ds"))]
                                prev,
                                #[cfg(not(feature = "o3ds"))]
                                n,
                                |this| {
                                    /* Pre-process */
                                    if !this
                                        .worker
                                        .data
                                        .pre_process_quarter_rem::<DCTSIZE>(&mut src_iter, rem)
                                    {
                                        return false;
                                    }
                                    #[cfg(not(feature = "o3ds"))]
                                    if n == j_max_half_factor(n) {
                                        pre_progress();
                                    }
                                    true
                                },
                                || {},
                            );
                            progress(rem);
                        }
                    } else {
                        // vss == false
                        if hss {
                            process(DOWNSAMPLE_FACTOR, |f, i| unsafe {
                                (&mut *f).pre_process_quarter_novsamp::<DCTSIZE>(&mut *i, 0)
                            });
                        } else {
                            process(DOWNSAMPLE_FACTOR, |f, i| unsafe {
                                (&mut *f).pre_process_quarter_nohsamp_novsamp::<DCTSIZE>(&mut *i, 0)
                            });
                        }
                    }
                }
                RP_DOWNSAMPLE_EVEN_ODD => {
                    if vss {
                        // vss == true
                        process(SAMP_FACTOR, |f, i| unsafe {
                            (&mut *f).pre_process_even_odd::<DCTSIZE>(&mut *i, 0)
                        });
                    } else {
                        // vss == false
                        if hss {
                            process(1, |f, i| unsafe {
                                (&mut *f).pre_process_even_odd_novsamp::<DCTSIZE, true>(&mut *i, 0)
                            });
                        } else {
                            process(1, |f, i| unsafe {
                                (&mut *f).pre_process_even_odd_novsamp::<DCTSIZE, false>(&mut *i, 0)
                            });
                        }
                    }
                }
                _ => {
                    if vss {
                        // vss == true
                        process(SAMP_FACTOR, |f, i| unsafe {
                            (&mut *f).pre_process_full::<DCTSIZE>(&mut *i, 0)
                        });
                    } else {
                        // vss == false
                        if hss {
                            process(1, |f, i| unsafe {
                                (&mut *f).pre_process_full_novsamp::<DCTSIZE, true>(&mut *i, 0)
                            });
                        } else {
                            process(1, |f, i| unsafe {
                                (&mut *f).pre_process_full_novsamp::<DCTSIZE, false>(&mut *i, 0)
                            });
                        }
                    }
                }
            }
        }

        self.flush_mcu();

        #[cfg(not(feature = "o3ds"))]
        if !self.worker.data.shared.rel_stream {
            if self.worker.data.thread_index
                == thread_index_last(self.worker.data.shared.core_count)
            {
                self.write_trailer();
            } else {
                self.write_rst();
            }
        }
        #[cfg(feature = "o3ds")]
        self.write_trailer();

        self.write_term();

        #[cfg(not(feature = "o3ds"))]
        let ret = Some(if self.worker.data.shared.delta_prog {
            JpegEncodeRet::JpegDqRet(JpegDqRet {
                delta_q: unsafe {
                    let jpeg_shared_mut = &mut *self.worker.jpeg_shared_mut;
                    let delta_q = *jpeg_shared_mut.work_delta_q.get(&w);
                    let shared_mut = &mut *self.worker.shared_mut;
                    let shared = self.worker.data.shared;
                    screen_work_release(
                        shared_mut,
                        shared,
                        w,
                        s,
                        self.worker.data.shared.core_count,
                    );

                    delta_q
                },
            })
        } else {
            JpegEncodeRet::JpegRet(JpegRet {
                quality: *is_top_index(is_top).index_into(&self.worker.jpeg_shared.quality) as u8,
                mcus: jpeg_screen.mcus,
            })
        });
        #[cfg(feature = "o3ds")]
        let ret = Some(());

        ret
    }

    fn process<F, G>(
        &mut self,
        #[cfg(not(feature = "o3ds"))] prev: *mut JBlock,
        #[cfg(not(feature = "o3ds"))] row_i: usize,
        do_pre_process: F,
        do_progress: G,
    ) where
        F: FnOnce(&mut Self) -> bool,
        G: FnOnce(),
    {
        if !do_pre_process(self) {
            return;
        }

        #[cfg(not(feature = "o3ds"))]
        let is_top = self.worker.data.info.is_top;
        #[cfg(not(feature = "o3ds"))]
        let screen = is_top_index(is_top).index_into(&self.worker.jpeg_shared.screens);

        /* Compress and encode */
        #[cfg(not(feature = "o3ds"))]
        self.do_process(
            if self.worker.data.shared.delta_prog {
                unsafe { prev.add(row_i * screen.mcus_per_row * screen.max_blocks_in_mcu) }
            } else {
                ptr::null_mut()
            },
            row_i as u8,
        );

        #[cfg(feature = "o3ds")]
        self.do_process();

        do_progress();
    }

    #[named]
    #[allow(unused_macros)]
    fn do_process(
        &mut self,
        #[cfg(not(feature = "o3ds"))] prev: *mut JBlock,
        #[cfg(not(feature = "o3ds"))] row_i: u8,
    ) {
        #[cfg(not(feature = "o3ds"))]
        let mut delta_cache = false;
        let is_top = self.worker.data.info.is_top;
        let screen = is_top_index(is_top).index_into(&self.worker.jpeg_shared.screens);
        for mcu_col_num in 0..screen.mcus_per_row {
            #[cfg(not(feature = "o3ds"))]
            if self.worker.data.shared.delta_prog {
                let s = is_top_index(self.worker.data.info.is_top);
                let w = self.worker.data.info.work_index;
                let jpeg_shared_mut = unsafe { &mut *self.worker.jpeg_shared_mut };
                let shared_mut = unsafe { &mut *self.worker.shared_mut };
                let shared = self.worker.data.shared;

                if row_i == 0 && mcu_col_num == 0 {
                    if !screen_work_acquire(
                        shared_mut,
                        shared,
                        w,
                        s,
                        self.worker.data.info.core_count,
                        self.worker.data.info.restart_interval,
                        || {
                            self.compute_dq(prev);
                            unsafe {
                                (*self.worker.shared_mut)
                                    .compressed_size
                                    .get(&s)
                                    .get_unchecked(self.worker.data.info.even_odd as usize)
                                    .store(0, Ordering::Release)
                            };
                            delta_cache = true;
                        },
                    ) {
                        return;
                    }
                }

                let dq_rescale_prev = *jpeg_shared_mut.dq_rescale_prev.get(&w);
                let prev = unsafe { prev.add(mcu_col_num * screen.max_blocks_in_mcu) };

                if dq_rescale_prev > 0 {
                    self.compress(mcu_col_num, prev, delta_cache, true, false);
                } else if dq_rescale_prev < 0 {
                    self.compress(mcu_col_num, prev, delta_cache, true, true);
                } else {
                    self.compress(mcu_col_num, prev, delta_cache, false, false);
                }
            } else {
                self.compress(mcu_col_num, ptr::null_mut(), false, false, false);
            }

            #[cfg(feature = "o3ds")]
            self.compress(mcu_col_num);

            self.encode_mcu();
        }
    }
}

#[cfg(not(feature = "o3ds"))]
#[named]
fn screen_work_release(
    shared_mut: &mut CommonSharedMut,
    shared: &EncoderShared,
    w: WorkIndex,
    s: ScreenIndex,
    core_count: CoreCount,
) {
    let c = shared_mut.work_sem_count.get_mut(&w);

    if c.fetch_sub(1, Ordering::AcqRel) == 1 {
        c.store(core_count.get() as u8, Ordering::Release);
        shared_mut
            .work_inited
            .get_mut(&w)
            .store(false, Ordering::Release);

        if rp_need_core_syn!() {
            let b = shared_mut.screen_bool.get_mut(&s);
            if b.swap(true, Ordering::AcqRel) {
                b.store(false, Ordering::Release);
            } else {
                unsafe { release_sem(cname!(), *shared.screen_sem.get(&s), c_str!("screen_sem")) };
            }
        }
    }
}

#[cfg(not(feature = "o3ds"))]
#[named]
fn screen_work_acquire(
    shared_mut: &mut CommonSharedMut,
    shared: &EncoderShared,
    w: WorkIndex,
    s: ScreenIndex,
    core_count: CoreCount,
    restart_interval: u16,
    f: impl FnOnce() -> (),
) -> bool {
    if !shared_mut.work_inited.get(&w).swap(true, Ordering::AcqRel) {
        let last_restart_interval = shared_mut.last_restart_interval.get_mut(&s);

        if rp_need_core_syn!() {
            let b = shared_mut.screen_bool.get(&s);
            let need_sync = restart_interval != *last_restart_interval;

            let need_sync = if !need_sync {
                b.swap(true, Ordering::AcqRel)
            } else {
                need_sync
            };

            if need_sync {
                if wait_syn(cname!(), *shared.screen_sem.get(&s), c_str!("screen_sem")).is_none() {
                    return false;
                }

                b.store(false, Ordering::Release);
                *last_restart_interval = restart_interval;
            }
        }

        f();

        unsafe {
            release_sem_count(
                cname!(),
                *shared.work_sem.get(&w),
                c_str!("work_sem"),
                core_count.get() as s32 - 1,
            );
        }
    } else {
        if wait_syn(cname!(), *shared.work_sem.get(&w), c_str!("work_sem")).is_none() {
            return false;
        }
    }

    true
}

#[cfg(not(feature = "o3ds"))]
#[named]
fn empty_work_acquire(shared: &EncoderShared, w: WorkIndex) -> Option<()> {
    wait_syn(cname!(), *shared.work_sem.get(&w), c_str!("work_sem"))?;
    Some(())
}

#[cfg(not(feature = "o3ds"))]
pub const fn j_max_half_factor(v: usize) -> usize {
    v / 2
}

pub struct LosslessEncode<'a, 'b> {
    pub worker: &'b mut LosslessWorker<'a>,
    pub dst: WorkerDst,
}

impl<'a, 'b> LosslessEncode<'a, 'b> {
    #[named]
    #[allow(unused_macros)]
    pub fn lossless_encode<F, G>(
        &mut self,
        #[cfg(not(feature = "mem3"))] src: &[u8],
        #[cfg(feature = "mem3")] src: *const u8,
        #[cfg(feature = "mem3")] pitch: u32,
        #[cfg(not(feature = "o3ds"))] mut pre_progress: F,
        mut progress: G,
    ) -> Option<LosslessEncodeRet>
    where
        F: FnMut(),
        G: FnMut(usize),
    {
        #[cfg(not(feature = "mem3"))]
        let bpp = get_bpp_for_format(self.worker.data.info.color_space);
        let is_top = self.worker.data.info.is_top;
        #[cfg(not(feature = "o3ds"))]
        let w = self.worker.data.info.work_index;
        let s = is_top_index(is_top);
        let screen = s.index_into(&self.worker.data.shared.screens);
        #[cfg(not(feature = "mem3"))]
        let pitch = GSP_SCREEN_WIDTH as usize * bpp as usize;

        let hss = screen.max_h_samp_factor == SAMP_FACTOR;
        let vss = screen.max_v_samp_factor == SAMP_FACTOR;

        self.worker.data.bufs.data.lossless.ptr = ptr::null();

        #[cfg(not(feature = "mem3"))]
        let height = src.len() / pitch;
        #[cfg(not(feature = "mem3"))]
        let mut src_iter = src.chunks_exact(pitch).map(|x| x.as_ptr());

        #[cfg(feature = "mem3")]
        let height = if is_top {
            GSP_SCREEN_HEIGHT_TOP
        } else {
            GSP_SCREEN_HEIGHT_BOTTOM
        } as usize;
        #[cfg(feature = "mem3")]
        let mut src_iter = {
            let src = unsafe { slice::from_raw_parts(src, pitch as usize * height) };
            src.chunks_exact(pitch as usize).map(|x| x.as_ptr())
        };

        #[cfg(not(feature = "o3ds"))]
        let rel_stream = self.worker.data.shared.rel_stream;
        #[cfg(feature = "o3ds")]
        let rel_stream = false;

        self.worker.data.bit_enc_state.reset();

        let mut localbuf: [u8; 0] = const_default();
        let mut buf = unsafe {
            EncodeBuffer::init(
                &mut *(&mut self.worker.data.bit_enc_state as *mut _),
                &mut *(&mut self.dst as *mut _),
                &mut localbuf,
                true,
            )
        };
        let b = &mut buf as *mut _;

        #[cfg(not(feature = "o3ds"))]
        let prev = unsafe {
            if self.worker.data.shared.delta_prog {
                if src.len() == 0 {
                    empty_work_acquire(self.worker.data.shared, w)?;
                }

                let shared_mut = &mut *self.worker.lossless_shared_mut;

                let prev = if is_top {
                    shared_mut.prev_coeffs_top.as_mut_ptr()
                } else {
                    shared_mut.prev_coeffs_bot.as_mut_ptr()
                };

                let prev = match screen.downsample {
                    RP_DOWNSAMPLE_CHECKER | RP_DOWNSAMPLE_EVEN_ODD => {
                        let count = (if is_top {
                            shared_mut.prev_coeffs_top.len()
                        } else {
                            shared_mut.prev_coeffs_bot.len()
                        }) / 2;
                        prev.add(count * self.worker.data.info.even_odd as usize)
                    }
                    RP_DOWNSAMPLE_QUARTER | _ => prev,
                };

                prev.add(
                    self.worker.data.info.restart_interval as usize
                        * LOSSLESS_BLOCK_SIZE
                        * downsample_screen_width(screen.downsample)
                        * MAX_COMPONENTS
                        * self.worker.data.thread_index.get() as usize,
                )
            } else {
                ptr::null_mut()
            }
        };

        let mut process =
            |buf: *mut EncodeBuffer<0>, f: usize, proc: fn(*mut WorkerCommon<'a>, _, _) -> ()| {
                let n = height / f;
                #[cfg(not(feature = "o3ds"))]
                let p_step = f * downsample_screen_width(screen.downsample) * MAX_COMPONENTS;
                #[allow(unused)]
                for i in 0..n {
                    self.process(
                        buf,
                        #[cfg(not(feature = "o3ds"))]
                        unsafe {
                            prev.add(i * p_step)
                        },
                        i,
                        |this| {
                            /* Pre-process */
                            proc(
                                &mut this.worker.data,
                                ptr::from_mut(&mut src_iter),
                                if rel_stream { i % 2 } else { 0 },
                            );
                            #[cfg(not(feature = "o3ds"))]
                            if i == j_max_half_factor(n) {
                                pre_progress();
                            }
                            true
                        },
                        || {},
                    );
                    progress(f);
                }
            };

        let mut novsamp_nosamp = |b: *mut EncodeBuffer<0>| {
            process(b, 1, |f, i, #[allow(unused)] n| unsafe {
                (&mut *f).lossless_pre_process_nohsamp_novsamp(&mut *i)
            });
        };

        match screen.downsample {
            RP_DOWNSAMPLE_QUARTER => {
                if vss {
                    // vss == true
                    process(b, SAMP_FACTOR * DOWNSAMPLE_FACTOR, |f, i, n| unsafe {
                        (&mut *f).pre_process_quarter::<1>(&mut *i, n)
                    });
                } else {
                    // vss == false
                    if hss {
                        process(b, DOWNSAMPLE_FACTOR, |f, i, n| unsafe {
                            (&mut *f).pre_process_quarter_novsamp::<1>(&mut *i, n)
                        });
                    } else {
                        if rel_stream {
                            process(b, DOWNSAMPLE_FACTOR, |f, i, n| unsafe {
                                (&mut *f).pre_process_quarter_nohsamp_novsamp::<1>(&mut *i, n)
                            });
                        } else {
                            process(b, DOWNSAMPLE_FACTOR, |f, i, #[allow(unused)] n| unsafe {
                                (&mut *f).lossless_pre_process_quarter_nohsamp_novsamp::<1>(&mut *i)
                            });
                        }
                    }
                }
            }
            RP_DOWNSAMPLE_EVEN_ODD => {
                if vss {
                    // vss == true
                    process(b, SAMP_FACTOR, |f, i, n| unsafe {
                        (&mut *f).pre_process_even_odd::<1>(&mut *i, n)
                    });
                } else {
                    // vss == false
                    if hss {
                        process(b, 1, |f, i, n| unsafe {
                            (&mut *f).pre_process_even_odd_novsamp::<1, true>(&mut *i, n)
                        });
                    } else {
                        // hss == false
                        if rel_stream {
                            process(b, 1, |f, i, n| unsafe {
                                (&mut *f).pre_process_even_odd_novsamp::<1, false>(&mut *i, n)
                            });
                        } else {
                            novsamp_nosamp(b);
                        }
                    }
                }
            }
            _ => {
                if vss {
                    // vss == true
                    process(b, SAMP_FACTOR, |f, i, n| unsafe {
                        (&mut *f).pre_process_full::<1>(&mut *i, n)
                    });
                } else {
                    // vss == false
                    if hss {
                        process(b, 1, |f, i, n| unsafe {
                            (&mut *f).pre_process_full_novsamp::<1, true>(&mut *i, n)
                        });
                    } else {
                        // hss == false
                        if rel_stream {
                            process(b, 1, |f, i, n| unsafe {
                                (&mut *f).pre_process_full_novsamp::<1, false>(&mut *i, n)
                            });
                        } else {
                            novsamp_nosamp(b);
                        }
                    }
                }
            }
        }

        buf.store();

        self.worker.data.bit_enc_state.flush(&mut self.dst, true);

        self.dst.term();

        #[cfg(not(feature = "o3ds"))]
        let bias = *s.index_into(&self.worker.lossless_shared.color_bias);
        #[cfg(not(feature = "o3ds"))]
        let bias = get_color_bias_from_format(bias, self.worker.data.info.color_space);

        #[cfg(not(feature = "o3ds"))]
        if self.worker.data.shared.delta_prog {
            let shared_mut = unsafe { &mut *self.worker.shared_mut };
            let shared = self.worker.data.shared;
            screen_work_release(shared_mut, shared, w, s, self.worker.data.shared.core_count);
        }

        Some(LosslessEncodeRet::LosslessRet(LosslessRet {
            #[cfg(not(feature = "o3ds"))]
            color_bias: bias,
        }))
    }

    fn process<F, G>(
        &mut self,
        buf: *mut EncodeBuffer<0>,
        #[cfg(not(feature = "o3ds"))] prev: *mut u8,
        i: usize,
        do_pre_process: F,
        do_progress: G,
    ) where
        F: FnOnce(&mut Self) -> bool,
        G: FnOnce(),
    {
        if !do_pre_process(self) {
            return;
        }

        /* Compress and encode */
        #[cfg(not(feature = "o3ds"))]
        self.do_process(buf, prev, i);

        #[cfg(feature = "o3ds")]
        self.do_process(buf, i);

        do_progress();
    }

    #[named]
    #[allow(unused_macros)]
    fn do_process(
        &mut self,
        #[allow(unused)] buf: *mut EncodeBuffer<0>,
        #[cfg(not(feature = "o3ds"))] prev: *mut u8,
        #[allow(unused)] i: usize,
    ) {
        unsafe {
            #[cfg(not(feature = "o3ds"))]
            if self.worker.data.shared.rel_stream {
                if self.worker.data.shared.delta_prog {
                    let s = is_top_index(self.worker.data.info.is_top);
                    let w = self.worker.data.info.work_index;
                    let shared_mut = &mut *self.worker.shared_mut;
                    let shared = self.worker.data.shared;
                    if i == 0 {
                        if !screen_work_acquire(
                            shared_mut,
                            shared,
                            w,
                            s,
                            self.worker.data.info.core_count,
                            self.worker.data.info.restart_interval,
                            || {},
                        ) {
                            return;
                        }
                    }
                    self.compressed_delta_encode(buf, prev, i);
                    return;
                }

                self.compressed_encode(buf, i);
                return;
            }

            if !self.worker.data.bufs.data.lossless.ptr.is_null() {
                self.copy_encode();
                return;
            }

            self.uncompressed_encode();
        }
    }
}
