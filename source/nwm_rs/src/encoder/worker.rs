// Contains code modified from github.com/libjpeg-turbo/libjpeg-turbo for use in NTR-HR
// See LICENSE-libjpeg-turbo.md at the project root for license details

use super::*;

pub type BitBufType = u32;

#[derive(ConstDefault)]
pub struct BitEncoderState {
    pub c: BitBufType,
    pub free_bits: isize,
}

impl BitEncoderState {
    pub fn reset(&mut self) {
        *self = const_default();
        self.free_bits = BIT_BUF_SIZE as isize;
    }

    pub fn flush(&mut self, dst: &mut WorkerDst, no_escape: bool) {
        let mut put_bits = BIT_BUF_SIZE as isize - self.free_bits;

        let mut localbuf: [u8; mem::size_of::<BitBufType>() * 4] = const_default();
        let put_buffer = self.c;
        let mut buf = EncodeBuffer::<_>::init(self, dst, &mut localbuf, no_escape);

        while put_bits >= 8 {
            put_bits -= 8;
            let temp = unsafe { core::intrinsics::unchecked_shr(put_buffer, put_bits) };
            unsafe { buf.emit_byte(temp as u8) }
        }
        if put_bits > 0 {
            /* fill partial byte with ones */
            let temp = (put_buffer << (8 - put_bits))
                | unsafe { core::intrinsics::unchecked_shr(0xFF, put_bits) };
            unsafe { buf.emit_byte(temp as u8) }
        }

        buf.store();
    }
}

pub const BIT_BUF_SIZE: usize = mem::size_of::<BitBufType>() * 8;

pub struct JpegWorker<'a> {
    pub data: WorkerCommon<'a>,
    pub jpeg_shared: &'a JpegShared,
    #[cfg(not(feature = "o3ds"))]
    pub jpeg_shared_mut: *mut JpegSharedMut,
    #[cfg(not(feature = "o3ds"))]
    pub shared_mut: *mut CommonSharedMut,
    pub last_dc_vals: LastDcVals,
}

pub type LastDcVals = [s16; MAX_COMPONENTS];

#[cfg(not(feature = "o3ds"))]
impl<'a> JpegWorker<'a> {
    pub fn encode<F, G>(
        &'a mut self,
        dst: WorkerDst,
        src: &[u8],
        pre_progress: F,
        progress: G,
    ) -> Option<JpegEncodeRet>
    where
        F: FnMut(),
        G: FnMut(usize),
    {
        JpegEncode { worker: self, dst }.encode(src, pre_progress, progress)
    }
}

#[cfg(all(feature = "o3ds", not(feature = "mem3")))]
impl<'a> JpegWorker<'a> {
    pub fn encode<G>(&'a mut self, dst: WorkerDst, src: &[u8], progress: G) -> Option<()>
    where
        G: FnMut(usize),
    {
        JpegEncode { worker: self, dst }.encode::<fn() -> (), _>(src, progress)
    }
}

#[cfg(all(feature = "o3ds", feature = "mem3"))]
impl<'a> JpegWorker<'a> {
    pub fn encode<G>(
        &'a mut self,
        dst: WorkerDst,
        src: *const u8,
        pitch: u32,
        progress: G,
    ) -> Option<()>
    where
        G: FnMut(usize),
    {
        JpegEncode { worker: self, dst }.encode::<fn() -> (), _>(src, pitch, progress)
    }
}

#[derive(ConstDefault, Clone, Copy)]
pub struct CInfo {
    pub is_top: bool,
    pub color_space: ColorSpace,
    #[cfg(not(feature = "mem3"))]
    pub restart_interval: u16,
    pub work_index: WorkIndex,
    #[cfg(not(feature = "o3ds"))]
    pub core_count: CoreCount,
    pub even_odd: bool,
}

impl Encoder {
    pub fn set_info(&mut self, info: CInfo) {
        *info.work_index.index_into_mut(&mut self.info) = info;
    }

    pub unsafe fn get_worker<'a>(
        &'a mut self,
        work_index: WorkIndex,
        thread_index: ThreadIndex,
    ) -> JpegWorker<'a> {
        JpegWorker {
            data: WorkerCommon {
                shared: &self.shared,
                bufs: thread_index.index_into_mut(&mut self.bufs),
                info: work_index.index_into_mut(&mut self.info),
                #[cfg(not(feature = "mem3"))]
                thread_index,
                bit_enc_state: const_default(),
            },
            jpeg_shared: &self.jpeg_shared,
            #[cfg(not(feature = "o3ds"))]
            jpeg_shared_mut: unsafe { &mut self.encoder_shared_mut.jpeg as *mut _ } as *mut _,
            #[cfg(not(feature = "o3ds"))]
            shared_mut: &mut self.common_shared_mut,
            last_dc_vals: const_default(),
        }
    }

    pub fn worker_dst(
        &self,
        #[cfg(not(feature = "o3ds"))] s: ScreenIndex,
        #[cfg(not(feature = "o3ds"))] w: WorkIndex,
        dst: *mut u8,
        #[cfg(not(feature = "o3ds"))] user: WorkderDstUser,
    ) -> WorkerDst {
        WorkerDst {
            blkn: 0,
            #[cfg(not(feature = "o3ds"))]
            s,
            #[cfg(not(feature = "o3ds"))]
            w,
            dst: dst as *mut u8,
            free_in_bytes: entries::thread_nwm::get_packet_data_size() as u16,
            #[cfg(not(feature = "o3ds"))]
            user,
            #[cfg(not(feature = "o3ds"))]
            rel_stream: self.shared.rel_stream,
            #[cfg(not(feature = "o3ds"))]
            delta_prog: self.shared.delta_prog,
            #[cfg(not(feature = "o3ds"))]
            even_odd: w.index_into(&self.info).even_odd,
            #[cfg(feature = "o3ds")]
            even_odd: WorkIndex::init(0).index_into(&self.info).even_odd,
        }
    }

    pub unsafe fn get_lossless_worker<'a>(
        &'a mut self,
        work_index: WorkIndex,
        thread_index: ThreadIndex,
    ) -> LosslessWorker<'a> {
        LosslessWorker {
            data: WorkerCommon {
                shared: &self.shared,
                bufs: thread_index.index_into_mut(&mut self.bufs),
                info: work_index.index_into_mut(&mut self.info),
                #[cfg(not(feature = "mem3"))]
                thread_index,
                bit_enc_state: const_default(),
            },
            lossless_shared: &self.lossless_shared,
            #[cfg(not(feature = "o3ds"))]
            shared_mut: &mut self.common_shared_mut,
            #[cfg(not(feature = "o3ds"))]
            lossless_shared_mut: unsafe { &mut self.encoder_shared_mut.lossless as *mut _ }
                as *mut _,
        }
    }
}

#[derive(ConstDefault)]
pub struct WorkerCommon<'a> {
    pub shared: &'a EncoderShared,
    pub bufs: &'a mut WorkerBufs,
    pub info: &'a CInfo,
    #[cfg(not(feature = "mem3"))]
    pub thread_index: ThreadIndex,
    pub bit_enc_state: BitEncoderState,
}

#[derive(ConstDefault)]
pub struct LosslessWorker<'a> {
    pub data: WorkerCommon<'a>,
    pub lossless_shared: &'a LosslessShared,
    #[cfg(not(feature = "o3ds"))]
    pub shared_mut: *mut CommonSharedMut,
    #[cfg(not(feature = "o3ds"))]
    pub lossless_shared_mut: *mut LosslessSharedMut,
}

impl<'a> LosslessWorker<'a> {
    #[cfg(not(feature = "o3ds"))]
    pub fn encode_lossless<F, G>(
        &'a mut self,
        dst: WorkerDst,
        src: &[u8],
        row_start: u32,
        pre_progress: F,
        progress: G,
    ) -> Option<LosslessEncodeRet>
    where
        F: FnMut(),
        G: FnMut(usize),
    {
        LosslessEncode { worker: self, dst }.lossless_encode(src, row_start, pre_progress, progress)
    }

    #[cfg(all(feature = "o3ds", not(feature = "mem3")))]
    pub fn encode_lossless<G>(
        &'a mut self,
        dst: WorkerDst,
        src: &[u8],
        progress: G,
    ) -> Option<LosslessEncodeRet>
    where
        G: FnMut(usize),
    {
        LosslessEncode { worker: self, dst }.lossless_encode::<fn() -> (), _>(src, progress)
    }

    #[cfg(all(feature = "o3ds", feature = "mem3"))]
    pub fn encode_lossless<G>(
        &'a mut self,
        dst: WorkerDst,
        src: *const u8,
        pitch: u32,
        progress: G,
    ) -> Option<LosslessEncodeRet>
    where
        G: FnMut(usize),
    {
        LosslessEncode { worker: self, dst }.lossless_encode::<fn() -> (), _>(src, pitch, progress)
    }
}
