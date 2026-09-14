// Contains code modified from github.com/libjpeg-turbo/libjpeg-turbo for use in NTR-HR
// See LICENSE-libjpeg-turbo.md at the project root for license details

use super::*;

impl<'a, 'b> JpegEncode<'a, 'b> {
    pub fn compress(
        &mut self,
        mcu_col_num: usize,
        #[cfg(not(feature = "o3ds"))] prev: *mut JBlock,
        #[cfg(not(feature = "o3ds"))] delta_cache: bool,
        #[cfg(not(feature = "o3ds"))] rescale_prev: bool,
        #[cfg(not(feature = "o3ds"))] rescale_prev_shr: bool,
    ) {
        #[cfg(not(feature = "o3ds"))]
        let w = self.worker.data.info.work_index;
        let mut blkn = 0;
        let is_top = self.worker.data.info.is_top;
        let s = is_top_index(is_top);
        let screen = s.index_into(&self.worker.data.shared.screens);
        let jpeg_screen = s.index_into(&self.worker.jpeg_shared.screens);
        let div_parts = &jpeg_screen.divisors.divisors;
        let hss = screen.max_h_samp_factor == SAMP_FACTOR;
        let vss = screen.max_v_samp_factor == SAMP_FACTOR;

        #[cfg(not(feature = "o3ds"))]
        let shared_mut = unsafe { &mut *self.worker.jpeg_shared_mut };

        #[cfg(not(feature = "o3ds"))]
        let cache = shared_mut.delta_q_cache.get_mut(&w);
        #[cfg(not(feature = "o3ds"))]
        let cache_next_i = shared_mut.delta_q_cache_next.get_mut(&w);
        #[cfg(not(feature = "o3ds"))]
        let delta_q = *shared_mut.work_delta_q.get(&w) as usize;
        #[cfg(not(feature = "o3ds"))]
        let delta_q0 = unsafe { self.worker.jpeg_shared.delta_q0_tbls.get_unchecked(delta_q) };
        #[cfg(not(feature = "o3ds"))]
        let mut delta_cache_start = 0;

        #[cfg(not(feature = "o3ds"))]
        let _need_wait_for_nwm =
            self.worker.data.shared.delta_prog && self.worker.data.thread_index.get() == 0;

        let comp_infos = unsafe { &**s.index_into(&self.worker.data.shared.comp_infos) };
        for ci in CompIndex::all() {
            let comp = ci.index_into(&comp_infos.infos);

            #[cfg(not(feature = "o3ds"))]
            let div_shifts = if self.worker.data.shared.delta_prog {
                unsafe {
                    self.worker
                        .jpeg_shared
                        .div_delta_q_shifts
                        .get_unchecked(delta_q)
                        .get_unchecked(comp.quant_tbl_no as usize)
                }
            } else {
                unsafe {
                    jpeg_screen
                        .div_shifts
                        .get_unchecked(comp.quant_tbl_no as usize)
                }
            };
            #[cfg(feature = "o3ds")]
            let div_shifts = unsafe {
                jpeg_screen
                    .div_shifts
                    .get_unchecked(comp.quant_tbl_no as usize)
            };

            #[cfg(not(feature = "o3ds"))]
            let rp_shifts = unsafe {
                shared_mut
                    .rp_shifts
                    .get(&w)
                    .get_unchecked(comp.quant_tbl_no as usize)
            };

            let mcu_width = comp.h_samp_factor;
            let mcu_height = comp.v_samp_factor;

            let mcu_sample_width = mcu_width as u16 * DCTSIZE as u16;
            let xpos = mcu_col_num as u16 * mcu_sample_width;
            let mut ypos = 0;

            for _ in 0..mcu_height {
                let mut xpos = xpos;
                for _ in 0..mcu_width {
                    #[cfg(not(feature = "o3ds"))]
                    let mut cache_hit = false;
                    #[cfg(feature = "o3ds")]
                    let cache_hit = false;
                    let output = unsafe {
                        self.worker
                            .data
                            .bufs
                            .data
                            .jpeg
                            .mcu
                            .get_unchecked_mut(blkn as usize)
                    };
                    #[cfg(not(feature = "o3ds"))]
                    let prev = if self.worker.data.shared.delta_prog {
                        unsafe { prev.add(blkn as usize) }
                    } else {
                        ptr::null_mut()
                    };

                    #[cfg(not(feature = "o3ds"))]
                    if delta_cache {
                        for qi in *cache_next_i.get(&ci)..*ci.index_into(&DELTA_Q_CACHE_COUNTS) {
                            let delta_cache_i = delta_cache_start + qi;
                            let cache = unsafe { cache.get_unchecked_mut(delta_cache_i as usize) };

                            if cache.ypos > ypos {
                                break;
                            }

                            if cache.ypos == ypos && cache.xpos > xpos {
                                break;
                            }

                            if cache.xpos == xpos && cache.ypos == ypos {
                                if delta_q == DELTA_Q_COUNT as usize - 1 {
                                    *output = cache.cache;
                                    unsafe { *prev = cache.next };
                                } else {
                                    let delta_q0 = unsafe {
                                        delta_q0.get_unchecked(comp.quant_tbl_no as usize)
                                    };
                                    for i in 0..DCTSIZE2 {
                                        unsafe {
                                            let (off_prev, off_diff) = if rescale_prev
                                                && rescale_prev_shr
                                            {
                                                let mask =
                                                    core::intrinsics::unchecked_shl(1, delta_q0[i])
                                                        - 1;
                                                let off_next = (cache.next[i] < 0) as JCoef
                                                    & ((cache.next[i] & mask) > 0) as JCoef;
                                                let off_prev = ((*prev)[i] < 0) as JCoef
                                                    & (((*prev)[i] & mask) > 0) as JCoef;
                                                let off_diff = (((*prev)[i] & mask)
                                                    > (cache.next[i] & mask))
                                                    as JCoef;
                                                (off_next, off_next - off_prev + off_diff)
                                            } else {
                                                let mask =
                                                    core::intrinsics::unchecked_shl(1, delta_q0[i])
                                                        - 1;
                                                let off_next = (cache.next[i] < 0) as JCoef
                                                    & ((cache.next[i] & mask) > 0) as JCoef;
                                                (off_next, off_next)
                                            };
                                            (*prev)[i] = core::intrinsics::unchecked_shr(
                                                cache.next[i],
                                                delta_q0[i],
                                            ) + off_prev;
                                            output[i] = core::intrinsics::unchecked_shr(
                                                cache.cache[i],
                                                delta_q0[i],
                                            ) + off_diff;
                                        }
                                    }
                                }
                                *cache_next_i.get_mut(&ci) = qi + 1;
                                cache_hit = true;
                                break;
                            }
                        }
                    }

                    if !cache_hit {
                        unsafe {
                            forward_dct(
                                #[cfg(not(feature = "o3ds"))]
                                self.worker.data.shared.delta_prog,
                                #[cfg(not(feature = "o3ds"))]
                                true,
                                #[cfg(not(feature = "o3ds"))]
                                rescale_prev,
                                #[cfg(not(feature = "o3ds"))]
                                rescale_prev_shr,
                                screen.downsample,
                                &self.worker.data.bufs.prep,
                                ci,
                                hss,
                                vss,
                                output,
                                ypos,
                                xpos,
                                div_parts.get_unchecked(comp.quant_tbl_no as usize),
                                div_shifts,
                                #[cfg(not(feature = "o3ds"))]
                                prev,
                                #[cfg(not(feature = "o3ds"))]
                                rp_shifts,
                                #[cfg(not(feature = "o3ds"))]
                                ptr::null_mut(),
                            );
                        }
                    }

                    xpos += DCTSIZE as u16;
                    blkn += 1;
                }
                ypos += DCTSIZE as u16;
            }

            #[cfg(not(feature = "o3ds"))]
            {
                delta_cache_start += *ci.index_into(&DELTA_Q_CACHE_COUNTS);
            }
        }
    }

    pub fn encode_mcu(&mut self) {
        let mut blkn = 0;

        let is_top = self.worker.data.info.is_top;
        let s = is_top_index(is_top);
        let comp_infos = unsafe { &**s.index_into(&self.worker.data.shared.comp_infos) };

        for ci in 0..MAX_COMPONENTS {
            let comp = &comp_infos.infos[ci];
            let mcu_width = comp.h_samp_factor;
            let mcu_height = comp.v_samp_factor;

            #[cfg(not(feature = "o3ds"))]
            let (dc_tbl, ac_tbl) = {
                let dc_tbl = if self.worker.data.shared.delta_prog {
                    unsafe {
                        self.worker
                            .data
                            .shared
                            .encode_tbls
                            .dq_entropy_tbls
                            .dc_derived_tbls
                            .get_unchecked(comp.dc_tbl_no as usize)
                    }
                } else {
                    unsafe {
                        self.worker
                            .data
                            .shared
                            .encode_tbls
                            .entropy_tbls
                            .dc_derived_tbls
                            .get_unchecked(comp.dc_tbl_no as usize)
                    }
                };
                let ac_tbl = if self.worker.data.shared.delta_prog {
                    unsafe {
                        self.worker
                            .data
                            .shared
                            .encode_tbls
                            .dq_entropy_tbls
                            .ac_derived_tbls
                            .get_unchecked(comp.ac_tbl_no as usize)
                    }
                } else {
                    unsafe {
                        self.worker
                            .data
                            .shared
                            .encode_tbls
                            .entropy_tbls
                            .ac_derived_tbls
                            .get_unchecked(comp.ac_tbl_no as usize)
                    }
                };
                (dc_tbl, ac_tbl)
            };

            #[cfg(feature = "o3ds")]
            let (dc_tbl, ac_tbl) = {
                unsafe {
                    (
                        self.worker
                            .data
                            .shared
                            .encode_tbls
                            .entropy_tbls
                            .dc_derived_tbls
                            .get_unchecked(comp.dc_tbl_no as usize),
                        self.worker
                            .data
                            .shared
                            .encode_tbls
                            .entropy_tbls
                            .ac_derived_tbls
                            .get_unchecked(comp.ac_tbl_no as usize),
                    )
                }
            };

            for _ in 0..mcu_height {
                for _ in 0..mcu_width {
                    let last_dc_val = self.worker.last_dc_vals[ci];
                    let dst = &mut self.dst;
                    let state = &mut self.worker.data.bit_enc_state;
                    let block = unsafe { self.worker.data.bufs.data.jpeg.mcu.get_unchecked(blkn) };
                    self.worker.last_dc_vals[ci] =
                        Self::encode_one_block(dst, state, block, last_dc_val, dc_tbl, ac_tbl);

                    blkn += 1;
                }
            }
        }
    }

    pub fn encode_one_block(
        dst: &mut WorkerDst,
        state: &mut BitEncoderState,
        block: &[i16; DCTSIZE2],
        last_dc_val: i16,
        dc_derived_tbl: &DerivedTbl,
        ac_derived_tbl: &DerivedTbl,
    ) -> i16 {
        dst.blkn += 1;

        /* Although it is exceedingly rare, it is possible for a Huffman-encoded
         * coefficient block to be larger than the 128-byte unencoded block.  For each
         * of the 64 coefficients, PUT_BITS is invoked twice, and each invocation can
         * theoretically store 16 bits (for a maximum of 2048 bits or 256 bytes per
         * encoded block.)  If, for instance, one artificially sets the AC
         * coefficients to alternating values of 32767 and -32768 (using the JPEG
         * scanning order-- 1, 8, 16, etc.), then this will produce an encoded block
         * larger than 200 bytes.
         */
        const BUFSIZE: usize = DCTSIZE2 * 8;

        let mut localbuf: [u8; BUFSIZE] = const_default();
        let mut buf = EncodeBuffer::init(
            state,
            dst,
            &mut localbuf,
            #[cfg(not(feature = "o3ds"))]
            dst.rel_stream,
            #[cfg(feature = "o3ds")]
            false,
        );

        let (val1, bits, b0) = {
            let val = block[0] as i32 - last_dc_val as i32;
            let sign1 = val >> (i32::BITS as u8 - 1);
            let val1 = val + sign1;
            let abs = val1 ^ sign1;
            (val1, jpeg_nbits_nonzero(abs) as i32, block[0])
        };

        unsafe {
            buf.put_code(
                *dc_derived_tbl.ehufco.get_unchecked(bits as usize),
                *dc_derived_tbl.ehufsi.get_unchecked(bits as usize),
                val1,
                bits,
            )
        };

        let mut r = 0;

        for jpeg_natural_order_of_k in JPEG_NATURAL_ORDER.into_iter().skip(1) {
            let val = *unsafe { block.get_unchecked(jpeg_natural_order_of_k as usize) } as i32;
            if val == 0 {
                r += 16;
            } else {
                let (val1, bits) = {
                    let sign1 = val >> (core::mem::size_of_val(&val) * 8 - 1);
                    let val1 = val + sign1;
                    let abs = val1 ^ sign1;
                    (val1, jpeg_nbits_nonzero(abs) as i32)
                };

                while r >= 16 * 16 {
                    r -= 16 * 16;
                    unsafe {
                        buf.put_bits(ac_derived_tbl.ehufco[0xf0], ac_derived_tbl.ehufsi[0xf0])
                    };
                }
                r += bits;
                unsafe {
                    buf.put_code(
                        *ac_derived_tbl.ehufco.get_unchecked(r as usize),
                        *ac_derived_tbl.ehufsi.get_unchecked(r as usize),
                        val1,
                        bits,
                    )
                };
                r = 0;
            }
        }

        if r > 0 {
            unsafe { buf.put_bits(ac_derived_tbl.ehufco[0], ac_derived_tbl.ehufsi[0]) };
        }

        buf.store();

        b0
    }
}

#[inline(always)]
fn color_bias_1(r: u8, g: u8, b: u8) -> u16 {
    // TODO optional use table
    let r = r >> 3;
    let g = g >> 2;
    let b = b >> 3;

    ((r as u16) << 11) | ((g as u16) << 5) | b as u16
}

#[inline(always)]
fn color_bias_2(r: u8, g: u8, b: u8) -> u16 {
    // TODO optional use table
    let r = r >> 4;
    let g = g >> 4;
    let b = b >> 4;

    ((r as u16) << 8) | ((g as u16) << 4) | b as u16
}

impl<'a, 'b> LosslessEncode<'a, 'b> {
    fn copy_rgb8<const R: usize, const G: usize, const B: usize, const P: usize, const T: usize>(
        &mut self,
    ) {
        let is_top = self.worker.data.info.is_top;
        let s = is_top_index(is_top);
        let screen = s.index_into(&self.worker.data.shared.screens);
        unsafe {
            match screen.downsample {
                RP_DOWNSAMPLE_NONE | RP_DOWNSAMPLE_EVEN_ODD => {
                    let (start, step) = {
                        if screen.downsample == RP_DOWNSAMPLE_NONE {
                            (0, 1)
                        } else if self.worker.data.info.even_odd == false {
                            (0, 2)
                        } else {
                            (1, 2)
                        }
                    };

                    let width = downsample_screen_width(RP_DOWNSAMPLE_NONE) / step;
                    let input = self.worker.data.bufs.data.lossless.ptr;

                    match *s.index_into(&self.worker.lossless_shared.color_bias) {
                        RP_COLOR_BIAS_NONE => {
                            for i in 0..width {
                                self.dst.write_bytes(slice::from_raw_parts(
                                    input.add((i * step + start) * P + T),
                                    3,
                                ));
                            }
                        }
                        RP_COLOR_BIAS_1 => {
                            for i in 0..width {
                                let input = input.add(i * P);
                                let output =
                                    color_bias_1(*input.add(R), *input.add(G), *input.add(B));
                                self.dst.write_bytes(slice::from_raw_parts(
                                    &output as *const u16 as *const u8,
                                    mem::size_of::<u16>(),
                                ));
                            }
                        }
                        RP_COLOR_BIAS_2 => {
                            let mut localbuf: [u8; 0] = const_default();
                            let mut buf = EncodeBuffer::init(
                                &mut self.worker.data.bit_enc_state,
                                &mut self.dst,
                                &mut localbuf,
                                true,
                            );
                            for i in 0..width {
                                let input = input.add((i * step + start) * P);
                                let output =
                                    color_bias_2(*input.add(R), *input.add(G), *input.add(B));
                                buf.put_bits(output as u32, 12);
                            }
                            buf.store();
                        }
                        _ => {}
                    }
                }

                _ => {}
            }
        }
    }

    fn copy_rgb565(&mut self) {
        let is_top = self.worker.data.info.is_top;
        let s = is_top_index(is_top);
        let screen = s.index_into(&self.worker.data.shared.screens);
        unsafe {
            match screen.downsample {
                RP_DOWNSAMPLE_NONE | RP_DOWNSAMPLE_EVEN_ODD => {
                    let (start, step) = {
                        if screen.downsample == RP_DOWNSAMPLE_NONE {
                            (0, 1)
                        } else if self.worker.data.info.even_odd == false {
                            (0, 2)
                        } else {
                            (1, 2)
                        }
                    };

                    let width = downsample_screen_width(RP_DOWNSAMPLE_NONE) / step;
                    let input = self.worker.data.bufs.data.lossless.ptr;
                    const P: usize = mem::size_of::<u16>();

                    match *s.index_into(&self.worker.lossless_shared.color_bias) {
                        RP_COLOR_BIAS_NONE | RP_COLOR_BIAS_1 => {
                            for i in 0..width {
                                let input = input.add((i * step + start) * P);
                                self.dst.write_bytes(slice::from_raw_parts(input, P));
                            }
                        }
                        RP_COLOR_BIAS_2 => {
                            let mut localbuf: [u8; 0] = const_default();
                            let mut buf = EncodeBuffer::init(
                                &mut self.worker.data.bit_enc_state,
                                &mut self.dst,
                                &mut localbuf,
                                true,
                            );
                            for i in 0..width {
                                let input = input.add((i * step + start) * P);
                                let input = *(input as *const u16);
                                let r = ((input >> 11) & 0x1f) << 3;
                                let g = ((input >> 5) & 0x3f) << 2;
                                let b = (input & 0x1f) << 3;
                                let output = color_bias_2(r as u8, g as u8, b as u8);
                                buf.put_bits(output as u32, 12);
                            }
                            buf.store();
                        }
                        _ => {}
                    }
                }

                _ => {}
            }
        }
    }

    fn copy_rgb5(&mut self) {
        let is_top = self.worker.data.info.is_top;
        let s = is_top_index(is_top);
        let screen = s.index_into(&self.worker.data.shared.screens);
        unsafe {
            match screen.downsample {
                RP_DOWNSAMPLE_NONE | RP_DOWNSAMPLE_EVEN_ODD => {
                    let (start, step) = {
                        if screen.downsample == RP_DOWNSAMPLE_NONE {
                            (0, 1)
                        } else if self.worker.data.info.even_odd == false {
                            (0, 2)
                        } else {
                            (1, 2)
                        }
                    };

                    let width = downsample_screen_width(RP_DOWNSAMPLE_NONE) / step;
                    let input = self.worker.data.bufs.data.lossless.ptr;
                    const P: usize = mem::size_of::<u16>();

                    match *s.index_into(&self.worker.lossless_shared.color_bias) {
                        RP_COLOR_BIAS_NONE | RP_COLOR_BIAS_1 => {
                            for i in 0..width {
                                let input = input.add((i * step + start) * P);
                                let input = *(input as *const u16);
                                let b = (input >> 1) & 0x1f;
                                let output = input & !0x3f | b;
                                self.dst.write_bytes(slice::from_raw_parts(
                                    &output as *const u16 as *const u8,
                                    2,
                                ));
                            }
                        }
                        RP_COLOR_BIAS_2 => {
                            let mut localbuf: [u8; 0] = const_default();
                            let mut buf = EncodeBuffer::init(
                                &mut self.worker.data.bit_enc_state,
                                &mut self.dst,
                                &mut localbuf,
                                true,
                            );
                            for i in 0..width {
                                let input = input.add((i * step + start) * P);
                                let input = *(input as *const u16);
                                let r = (input >> 12) & 0xf;
                                let g = (input >> 7) & 0xf;
                                let b = (input >> 2) & 0xf;
                                let output = (r << 8) | (g << 4) | b;
                                buf.put_bits(output as u32, 12);
                            }
                            buf.store();
                        }
                        _ => {}
                    }
                }

                _ => {}
            }
        }
    }

    fn copy_rgb4(&mut self) {
        let is_top = self.worker.data.info.is_top;
        let s = is_top_index(is_top);
        let screen = s.index_into(&self.worker.data.shared.screens);
        unsafe {
            match screen.downsample {
                RP_DOWNSAMPLE_NONE | RP_DOWNSAMPLE_EVEN_ODD => {
                    let (start, step) = {
                        if screen.downsample == RP_DOWNSAMPLE_NONE {
                            (0, 1)
                        } else if self.worker.data.info.even_odd == false {
                            (0, 2)
                        } else {
                            (1, 2)
                        }
                    };

                    let width = downsample_screen_width(RP_DOWNSAMPLE_NONE) / step;
                    let input = self.worker.data.bufs.data.lossless.ptr;
                    const P: usize = mem::size_of::<u16>();

                    match *s.index_into(&self.worker.lossless_shared.color_bias) {
                        RP_COLOR_BIAS_NONE | RP_COLOR_BIAS_1 | RP_COLOR_BIAS_2 => {
                            let mut localbuf: [u8; 0] = const_default();
                            let mut buf = EncodeBuffer::init(
                                &mut self.worker.data.bit_enc_state,
                                &mut self.dst,
                                &mut localbuf,
                                true,
                            );
                            for i in 0..width {
                                let input = input.add((i * step + start) * P);
                                let input = *(input as *const u16);
                                buf.put_bits((input >> 4) as u32, 12);
                            }
                            buf.store();
                        }
                        _ => {}
                    }
                }

                _ => {}
            }
        }
    }

    pub fn copy_encode(&mut self) {
        match self.worker.data.info.color_space {
            ColorSpace::RGBA8 => self.copy_rgb8::<3, 2, 1, 4, 1>(),
            ColorSpace::RGB8 => self.copy_rgb8::<2, 1, 0, 3, 0>(),
            ColorSpace::RGB565 => self.copy_rgb565(),
            ColorSpace::RGB5A1 => self.copy_rgb5(),
            ColorSpace::RGB4 => self.copy_rgb4(),
        }
    }

    fn do_uncompressed_encode<const HSS: bool, const VSS: bool>(&mut self) {
        unsafe {
            let is_top = self.worker.data.info.is_top;
            let s = is_top_index(is_top);
            let bias = *s.index_into(&self.worker.lossless_shared.color_bias);
            let bias = get_color_bias_from_format(bias, self.worker.data.info.color_space);
            let screen = s.index_into(&self.worker.data.shared.screens);
            let input = &self.worker.data.bufs.prep;
            let need_hss = |ci| {
                HSS && if VSS {
                    need_subsamp_ci::<true, true>(ci)
                } else {
                    need_subsamp_ci::<true, false>(ci)
                }
            };
            let i_x: [u32; 3] = core::array::from_fn(|i| i as u32);
            let ci_x = i_x.map(|x| CompIndex::init(x));
            let ci_hss_x = ci_x.map(|x| need_hss(x));
            let width = downsample_screen_width(screen.downsample);
            let width_x = ci_hss_x.map(|x| if x { width / SAMP_FACTOR } else { width });

            let (bw_x, bh_x) = if HSS {
                if VSS {
                    ([SAMP_FACTOR, 1, 1], [SAMP_FACTOR, 1, 1])
                } else {
                    ([SAMP_FACTOR, 1, 1], [1, 1, 1])
                }
            } else {
                ([1, 1, 1], [1, 1, 1])
            };

            let in_x = match screen.downsample {
                RP_DOWNSAMPLE_NONE => ci_x.map(|x| input.full.get(x, HSS, VSS).as_ptr()),
                RP_DOWNSAMPLE_EVEN_ODD => ci_x.map(|x| input.even_odd.get(x, HSS, VSS).as_ptr()),
                RP_DOWNSAMPLE_QUARTER => ci_x.map(|x| input.quarter.buf.get(x, HSS, VSS).as_ptr()),
                _ => todo!(),
            };

            match bias {
                RP_COLOR_BIAS_NONE => {
                    for x in 0..if HSS { width / SAMP_FACTOR } else { width } {
                        for c in CompIndex::all() {
                            let width_c = *c.index_into(&width_x);
                            let bw_c = *c.index_into(&bw_x) as usize;
                            let bh_c = *c.index_into(&bh_x) as usize;
                            let in_c = *c.index_into(&in_x);
                            let ix = x * bw_c as usize;
                            let iy = 0 as usize;
                            for by in 0..bh_c {
                                let iy = iy + by;
                                for bx in 0..bw_c {
                                    let ix = ix + bx;
                                    self.dst.write_byte(*in_c.add(iy * width_c + ix));
                                }
                            }
                        }
                    }
                }
                RP_COLOR_BIAS_1 | RP_COLOR_BIAS_2 => {
                    let bb_x = if bias == RP_COLOR_BIAS_1 {
                        [6, 5, 5]
                    } else {
                        [4, 4, 4]
                    };

                    let mut localbuf: [u8; 0] = const_default();
                    let mut buf = EncodeBuffer::init(
                        &mut self.worker.data.bit_enc_state,
                        &mut self.dst,
                        &mut localbuf,
                        true,
                    );

                    for x in 0..if HSS { width / SAMP_FACTOR } else { width } {
                        for c in CompIndex::all() {
                            let bb_c = *c.index_into(&bb_x);
                            let width_c = *c.index_into(&width_x);
                            let bw_c = *c.index_into(&bw_x) as usize;
                            let bh_c = *c.index_into(&bh_x) as usize;
                            let in_c = *c.index_into(&in_x);
                            let ix = x * bw_c as usize;
                            let iy = 0 as usize;
                            for by in 0..bh_c {
                                let iy = iy + by;
                                for bx in 0..bw_c {
                                    let ix = ix + bx;
                                    let in_c = *in_c.add(iy * width_c + ix);
                                    let in_c = in_c >> (8 - bb_c);
                                    buf.put_bits(in_c as u32, bb_c);
                                }
                            }
                        }
                    }

                    buf.store();
                }
                _ => {}
            }
        }
    }

    pub fn uncompressed_encode(&mut self) {
        let is_top = self.worker.data.info.is_top;
        let s = is_top_index(is_top);
        let screen = s.index_into(&self.worker.data.shared.screens);
        let hss = screen.max_h_samp_factor == SAMP_FACTOR;
        let vss = screen.max_v_samp_factor == SAMP_FACTOR;
        if hss {
            if vss {
                self.do_uncompressed_encode::<true, true>();
            } else {
                self.do_uncompressed_encode::<true, false>();
            }
        } else {
            self.do_uncompressed_encode::<false, false>();
        }
    }

    #[cfg(not(feature = "o3ds"))]
    fn do_compressed_encode<const HSS: bool, const VSS: bool>(
        &mut self,
        buf: *mut EncodeBuffer<0>,
        i: usize,
    ) {
        unsafe {
            let is_top = self.worker.data.info.is_top;
            let s = is_top_index(is_top);
            let bias = *s.index_into(&self.worker.lossless_shared.color_bias);
            let bias = get_color_bias_from_format(bias, self.worker.data.info.color_space);
            let screen = s.index_into(&self.worker.data.shared.screens);
            let input = &self.worker.data.bufs.prep;
            let need_hss = |ci| {
                HSS && if VSS {
                    need_subsamp_ci::<true, true>(ci)
                } else {
                    need_subsamp_ci::<true, false>(ci)
                }
            };
            let i_x: [u32; 3] = core::array::from_fn(|i| i as u32);
            let ci_x = i_x.map(|x| CompIndex::init(x));
            let ci_hss_x = ci_x.map(|x| need_hss(x));
            let width = downsample_screen_width(screen.downsample);
            let width_x = ci_hss_x.map(|x| if x { width / SAMP_FACTOR } else { width });

            let (bw_x, bh_x) = if HSS {
                if VSS {
                    ([SAMP_FACTOR, 1, 1], [SAMP_FACTOR, 1, 1])
                } else {
                    ([SAMP_FACTOR, 1, 1], [1, 1, 1])
                }
            } else {
                ([1, 1, 1], [1, 1, 1])
            };

            let in_x = match screen.downsample {
                RP_DOWNSAMPLE_NONE => ci_x.map(|x| input.full.get(x, HSS, VSS).as_ptr()),
                RP_DOWNSAMPLE_EVEN_ODD => ci_x.map(|x| input.even_odd.get(x, HSS, VSS).as_ptr()),
                RP_DOWNSAMPLE_QUARTER => ci_x.map(|x| input.quarter.buf.get(x, HSS, VSS).as_ptr()),
                _ => todo!(),
            };

            let bb_x = match bias {
                RP_COLOR_BIAS_NONE => [8, 8, 8],
                RP_COLOR_BIAS_1 => [6, 5, 5],
                RP_COLOR_BIAS_2 => [4, 4, 4],
                _ => panic!(),
            };

            let etbl = &self.worker.data.shared.encode_tbls.lossless_entropy_tbl;

            for c in CompIndex::all() {
                let bb_c = *c.index_into(&bb_x);
                let width_c = *c.index_into(&width_x);
                let bw_c = *c.index_into(&bw_x) as usize;
                let bh_c = *c.index_into(&bh_x) as usize;
                let in_c = *c.index_into(&in_x);

                let iy = (i % 2) * bh_c as usize;
                for by in 0..bh_c {
                    let iy = iy + by;
                    for ix in 0..if HSS { width / SAMP_FACTOR } else { width } * bw_c {
                        let in_c_c = in_c.add(iy * width_c + ix);
                        let in_t_c = if by > 0 {
                            in_c_c.sub(width_c)
                        } else if i > 0 {
                            if i % 2 == 0 {
                                in_c_c.add(width_c * (bh_c * 2 - 1))
                            } else {
                                in_c_c.sub(width_c)
                            }
                        } else {
                            ptr::null()
                        };
                        let (in_l_c, in_tl_c) = if ix > 0 {
                            (
                                in_c_c.sub(1),
                                if !in_t_c.is_null() {
                                    in_t_c.sub(1)
                                } else {
                                    ptr::null()
                                },
                            )
                        } else {
                            (ptr::null(), ptr::null())
                        };
                        let in_c = Self::pred_pixel(*in_c_c, in_t_c, in_l_c, in_tl_c, bb_c);
                        let name = lossless_tbl_name_from_bits(bb_c) as usize;
                        (*(buf as *mut EncodeBuffer<0>)).put_bits(
                            etbl.get_unchecked(name).ehufco[in_c as usize],
                            etbl.get_unchecked(name).ehufsi[in_c as usize],
                        );
                    }
                }
            }
        }
    }

    #[cfg(not(feature = "o3ds"))]
    fn median_3(a: u8, b: u8, c: u8) -> u8 {
        let max = cmp::max(cmp::max(a, b), c);
        let min = cmp::min(cmp::min(a, b), c);
        a + b + c - max - min
    }

    #[cfg(not(feature = "o3ds"))]
    fn pred_pixel(c: u8, t: *const u8, l: *const u8, tl: *const u8, b: u8) -> u8 {
        unsafe {
            let p = if t.is_null() {
                if l.is_null() { 128 } else { *l }
            } else {
                if l.is_null() {
                    *t
                } else {
                    Self::median_3(*t, *l, *tl)
                }
            };

            if b < 8 {
                let s = 8 - b;
                ((((c >> s) - (p >> s)) << s) as s8 >> s) as u8 + 128
            } else {
                c - p + 128
            }
        }
    }

    #[cfg(not(feature = "o3ds"))]
    pub fn compressed_encode(&mut self, buf: *mut EncodeBuffer<0>, i: usize) {
        let is_top = self.worker.data.info.is_top;
        let s = is_top_index(is_top);
        let screen = s.index_into(&self.worker.data.shared.screens);
        let hss = screen.max_h_samp_factor == SAMP_FACTOR;
        let vss = screen.max_v_samp_factor == SAMP_FACTOR;
        if hss {
            if vss {
                self.do_compressed_encode::<true, true>(buf, i);
            } else {
                self.do_compressed_encode::<true, false>(buf, i);
            }
        } else {
            self.do_compressed_encode::<false, false>(buf, i);
        }
    }

    #[cfg(not(feature = "o3ds"))]
    pub fn compressed_delta_encode(&mut self, buf: *mut EncodeBuffer<0>, prev: *mut u8, i: usize) {
        let is_top = self.worker.data.info.is_top;
        let s = is_top_index(is_top);
        let screen = s.index_into(&self.worker.data.shared.screens);
        let hss = screen.max_h_samp_factor == SAMP_FACTOR;
        let vss = screen.max_v_samp_factor == SAMP_FACTOR;
        if hss {
            if vss {
                self.do_compressed_delta_encode::<true, true>(buf, prev, i);
            } else {
                self.do_compressed_delta_encode::<true, false>(buf, prev, i);
            }
        } else {
            self.do_compressed_delta_encode::<false, false>(buf, prev, i);
        }
    }

    #[cfg(not(feature = "o3ds"))]
    fn do_compressed_delta_encode<const HSS: bool, const VSS: bool>(
        &mut self,
        buf: *mut EncodeBuffer<0>,
        prev: *mut u8,
        i: usize,
    ) {
        unsafe {
            let is_top = self.worker.data.info.is_top;
            let s = is_top_index(is_top);
            let bias = *s.index_into(&self.worker.lossless_shared.color_bias);
            let bias = get_color_bias_from_format(bias, self.worker.data.info.color_space);
            let screen = s.index_into(&self.worker.data.shared.screens);
            let input = &self.worker.data.bufs.prep;
            let need_hss = |ci| {
                HSS && if VSS {
                    need_subsamp_ci::<true, true>(ci)
                } else {
                    need_subsamp_ci::<true, false>(ci)
                }
            };
            let _h_samp = if HSS { SAMP_FACTOR } else { 1 };
            let v_samp = if VSS { SAMP_FACTOR } else { 1 };
            let i_x: [u32; 3] = core::array::from_fn(|i| i as u32);
            let ci_x = i_x.map(|x| CompIndex::init(x));
            let ci_hss_x = ci_x.map(|x| need_hss(x));
            let width = downsample_screen_width(screen.downsample);
            let width_x = ci_hss_x.map(|x| if x { width / SAMP_FACTOR } else { width });

            let (bw_x, bh_x) = if HSS {
                if VSS {
                    ([SAMP_FACTOR, 1, 1], [SAMP_FACTOR, 1, 1])
                } else {
                    ([SAMP_FACTOR, 1, 1], [1, 1, 1])
                }
            } else {
                ([1, 1, 1], [1, 1, 1])
            };

            let in_x = match screen.downsample {
                RP_DOWNSAMPLE_NONE => ci_x.map(|x| input.full.get(x, HSS, VSS).as_ptr()),
                RP_DOWNSAMPLE_EVEN_ODD => ci_x.map(|x| input.even_odd.get(x, HSS, VSS).as_ptr()),
                RP_DOWNSAMPLE_QUARTER => ci_x.map(|x| input.quarter.buf.get(x, HSS, VSS).as_ptr()),
                _ => todo!(),
            };

            let bb_x = match bias {
                RP_COLOR_BIAS_NONE => [8, 8, 8],
                RP_COLOR_BIAS_1 => [6, 5, 5],
                RP_COLOR_BIAS_2 => [4, 4, 4],
                _ => panic!(),
            };

            let etbl = &self.worker.data.shared.encode_tbls.lossless_entropy_tbl;

            const DELTA_BLOCK_WIDTH_COUNT: usize = 30;
            let mut pred = mem::MaybeUninit::<[u8; DELTA_BLOCK_WIDTH_COUNT]>::uninit();
            let mut diff = mem::MaybeUninit::<[u8; DELTA_BLOCK_WIDTH_COUNT]>::uninit();

            for c in CompIndex::all() {
                let bb_c = *c.index_into(&bb_x);
                let width_c = *c.index_into(&width_x);
                let bw_c = *c.index_into(&bw_x) as usize;
                let bh_c = *c.index_into(&bh_x) as usize;
                let in_c = *c.index_into(&in_x);

                let name = lossless_tbl_name_from_bits(bb_c) as usize;
                let tbl = etbl.get_unchecked(name);
                let prev = prev.add(v_samp * width * c.get() as usize);

                let iy = (i % 2) * bh_c as usize;
                for by in 0..bh_c {
                    let iy = iy + by;
                    let prev = prev.add(by * width);
                    for ix in (0..if HSS { width / SAMP_FACTOR } else { width } * bw_c)
                        .array_chunks::<DELTA_BLOCK_WIDTH_COUNT>()
                    {
                        let mut pred_len = 0;
                        let mut diff_len = 0;

                        for (ip, ix) in (0..DELTA_BLOCK_WIDTH_COUNT).zip(ix) {
                            let in_c_c = in_c.add(iy * width_c + ix);
                            let in_t_c = if by > 0 {
                                in_c_c.sub(width_c)
                            } else if i > 0 {
                                if i % 2 == 0 {
                                    in_c_c.add(width_c * (bh_c * 2 - 1))
                                } else {
                                    in_c_c.sub(width_c)
                                }
                            } else {
                                ptr::null()
                            };
                            let (in_l_c, in_tl_c) = if ix > 0 {
                                (
                                    in_c_c.sub(1),
                                    if !in_t_c.is_null() {
                                        in_t_c.sub(1)
                                    } else {
                                        ptr::null()
                                    },
                                )
                            } else {
                                (ptr::null(), ptr::null())
                            };
                            let in_c = Self::pred_pixel(*in_c_c, in_t_c, in_l_c, in_tl_c, bb_c);
                            *(*pred.as_mut_ptr()).get_unchecked_mut(ip) = in_c;
                            pred_len += tbl.ehufsi[in_c as usize] as usize;

                            let prev = prev.add(ix);
                            let diff_c = if bb_c < 8 {
                                let s = 8 - bb_c;
                                ((((*in_c_c >> s) - (*prev >> s)) << s) as s8 >> s) as u8 + 128
                            } else {
                                *in_c_c - *prev + 128
                            };
                            *prev = *in_c_c;
                            *(*diff.as_mut_ptr()).get_unchecked_mut(ip) = diff_c;
                            diff_len += tbl.ehufsi[diff_c as usize] as usize;
                        }

                        let buf = &mut *(buf as *mut EncodeBuffer<0>);
                        if diff_len < pred_len {
                            buf.put_bits(1, 1);
                            for in_c in diff.assume_init_ref() {
                                buf.put_bits(
                                    tbl.ehufco[*in_c as usize],
                                    tbl.ehufsi[*in_c as usize],
                                );
                            }
                        } else {
                            buf.put_bits(0, 1);
                            for in_c in pred.assume_init_ref() {
                                buf.put_bits(
                                    tbl.ehufco[*in_c as usize],
                                    tbl.ehufsi[*in_c as usize],
                                );
                            }
                        }
                    }
                }
            }
        }
    }
}

pub fn get_color_bias_from_format(bias: u8, format: ColorSpace) -> u8 {
    if false {
        cmp::max(
            bias,
            match format {
                ColorSpace::RGBA8 | ColorSpace::RGB8 => RP_COLOR_BIAS_NONE,
                ColorSpace::RGB565 | ColorSpace::RGB5A1 => RP_COLOR_BIAS_1,
                ColorSpace::RGB4 => RP_COLOR_BIAS_2,
            },
        )
    } else {
        bias
    }
}

pub fn get_color_space_from_format(format: u32) -> ColorSpace {
    match format {
        0 => ColorSpace::RGBA8,
        1 => ColorSpace::RGB8,
        2 => ColorSpace::RGB565,
        3 => ColorSpace::RGB5A1,
        4 => ColorSpace::RGB4,
        _ => panic!(),
    }
}
