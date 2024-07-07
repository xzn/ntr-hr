#![allow(dead_code)]

use crate::*;
mod vars;
use vars::{DCTSIZE, MAX_COMPONENTS, *};

#[derive(ConstDefault)]
pub struct JpegShared {
    huffTbls: HuffTbls,
    entropyTbls: EntropyTbls,
    colorConvTbls: ColorConvTabs,
    compInfos: CompInfos,
    quantTbls: QuantTbls,
    divisors: Divisors,
    coreCount: CoreCount,
}

#[derive(Clone)]
pub struct WorkerDst<'a> {
    dst: *mut u8,
    free_in_bytes: u16,
    phantom: PhantomData<&'a mut [u8]>,
}

impl<'a> WorkerDst<'a> {
    fn write_bytes(&mut self, bytes: &[u8]) {
        let mut src = bytes.as_ptr();
        let mut len = bytes.len() as u16;

        if len > 0 {
            loop {
                if self.free_in_bytes > 0 {
                    let bytes_to_copy = core::cmp::min(self.free_in_bytes, len);
                    unsafe {
                        ptr::copy_nonoverlapping(src, self.dst, bytes_to_copy as usize);
                    }

                    len -= bytes_to_copy;
                    src = unsafe { src.add(bytes_to_copy as usize) };
                    self.free_in_bytes -= bytes_to_copy;
                    self.dst = unsafe { self.dst.add(bytes_to_copy as usize) };
                }
                if len > 0 {
                    self.flush();
                } else {
                    break;
                }
            }
        }
    }

    fn flush(&mut self) {
        todo!()
    }

    fn term(&mut self) {
        todo!()
    }

    pub unsafe fn advance_to(&mut self, dst: *mut u8) {
        self.free_in_bytes -= dst.sub_ptr(self.dst) as u16;
        self.dst = dst;
    }
}

#[derive(ConstDefault)]
pub struct CInfo {
    isTop: bool,
    colorSpace: ColorSpace,
    restartInterval: u16,
    workIndex: WorkIndex,
}

type BitBufType = usize;

#[derive(ConstDefault)]
pub struct HuffState {
    c: BitBufType,
    free_bits: isize,
}

pub const BIT_BUF_SIZE: usize = mem::size_of::<BitBufType>() * 8;

#[derive(ConstDefault)]
pub struct JpegWorker<'a> {
    shared: &'a JpegShared,
    bufs: &'a mut WorkerBufs,
    info: &'a CInfo,
    threadId: ThreadId,
    huffState: HuffState,
    last_dc_val: [i16; MAX_COMPONENTS],
}

pub struct JpegEncode<'a, 'b, 'c> {
    worker: &'c mut JpegWorker<'a>,
    dst: WorkerDst<'b>,
}

type JpegWorkers<'a> = [JpegWorker<'a>; RP_CORE_COUNT_MAX as usize];

#[derive(ConstDefault)]
pub struct Jpeg {
    shared: JpegShared,
    bufs: [WorkerBufs; RP_CORE_COUNT_MAX as usize],
    info: CInfo,
}

impl Jpeg {
    pub fn init(&mut self) {
        self.shared.huffTbls.init();
        self.shared
            .entropyTbls
            .setEntropyTbls(&self.shared.huffTbls);
        self.shared.colorConvTbls.init();
        self.shared.compInfos.setColorSpaceYCbCr();
    }

    pub fn reset<'a>(&'a mut self, quality: u32) -> JpegWorkers<'a> {
        self.shared.quantTbls.setQuality(quality);
        self.shared.divisors.setDivisors(&self.shared.quantTbls);

        let [b0, b1, b2] = &mut self.bufs;
        [
            JpegWorker::init(&self.shared, b0, &self.info, ThreadId::init_val(0)),
            JpegWorker::init(&self.shared, b1, &self.info, ThreadId::init_val(1)),
            JpegWorker::init(&self.shared, b2, &self.info, ThreadId::init_val(2)),
        ]
    }
}

fn pconvert(r: u8, g: u8, b: u8, y: &mut u8, cb: &mut u8, cr: &mut u8, ctab: &[i32; TABLE_SIZE]) {
    /* If the inputs are 0.._MAXJSAMPLE, the outputs of these equations
     * must be too; we do not need an explicit range-limiting operation.
     * Hence the value being shifted is never negative, and we don't
     * need the general RIGHT_SHIFT macro.
     */
    /* Y */
    *y = ((ctab[r as usize + R_Y_OFF] + ctab[g as usize + G_Y_OFF] + ctab[b as usize + B_Y_OFF])
        >> SCALEBITS) as u8;
    /* Cb */
    *cb =
        ((ctab[r as usize + R_CB_OFF] + ctab[g as usize + G_CB_OFF] + ctab[b as usize + B_CB_OFF])
            >> SCALEBITS) as u8;
    /* Cr */
    *cr =
        ((ctab[r as usize + R_CR_OFF] + ctab[g as usize + G_CR_OFF] + ctab[b as usize + B_CR_OFF])
            >> SCALEBITS) as u8;
}

fn cconvert<const R: usize, const G: usize, const B: usize, const P: usize, const N: usize>(
    input: &[&[u8]; N],
    output: &mut [[[u8; vars::GSP_SCREEN_WIDTH]; N]; vars::MAX_COMPONENTS],
    tab: &[i32; TABLE_SIZE],
) {
    for i in 0..N {
        let input = input[i];
        let [output0, output1, output2] = output;
        let output0 = &mut output0[i];
        let output1 = &mut output1[i];
        let output2 = &mut output2[i];

        for (((input, output0), output1), output2) in input
            .array_chunks::<P>()
            .zip(output0.into_iter())
            .zip(output1.into_iter())
            .zip(output2.into_iter())
        {
            let r = input[R];
            let g = input[G];
            let b = input[B];

            pconvert(r, g, b, output0, output1, output2, tab);
        }
    }
}

fn cconvert2<const N: usize, F>(
    input: &[&[u8]; N],
    comps: F,
    output: &mut [[[u8; vars::GSP_SCREEN_WIDTH]; N]; vars::MAX_COMPONENTS],
    tab: &ColorConvTabs,
) where
    F: Fn(u16, &ColorConvTabs) -> (u8, u8, u8),
{
    for i in 0..N {
        let input = input[i];
        let [output0, output1, output2] = output;
        let output0 = &mut output0[i];
        let output1 = &mut output1[i];
        let output2 = &mut output2[i];

        for (((input, output0), output1), output2) in input
            .array_chunks::<2>()
            .zip(output0.into_iter())
            .zip(output1.into_iter())
            .zip(output2.into_iter())
        {
            let (r, g, b) = comps(input[0] as u16 | ((input[1] as u16) << 8), tab);

            pconvert(r, g, b, output0, output1, output2, &tab.rgb_ycc_tab);
        }
    }
}

fn rgb565_comps(input: u16, tab: &ColorConvTabs) -> (u8, u8, u8) {
    let r = tab.rb_5_tab[((input >> 11) & 0x1f) as usize];
    let g = tab.g_6_tab[((input >> 5) & 0x3f) as usize];
    let b = tab.rb_5_tab[(input & 0x1f) as usize];
    (r, g, b)
}
fn rgb5a1_comps(input: u16, tab: &ColorConvTabs) -> (u8, u8, u8) {
    let r = tab.rb_5_tab[((input >> 11) & 0x1f) as usize];
    let g = tab.rb_5_tab[((input >> 6) & 0x1f) as usize];
    let b = tab.rb_5_tab[((input >> 1) & 0x1f) as usize];
    (r, g, b)
}

/* Although it is exceedingly rare, it is possible for a Huffman-encoded
 * coefficient block to be larger than the 128-byte unencoded block.  For each
 * of the 64 coefficients, PUT_BITS is invoked twice, and each invocation can
 * theoretically store 16 bits (for a maximum of 2048 bits or 256 bytes per
 * encoded block.)  If, for instance, one artificially sets the AC
 * coefficients to alternating values of 32767 and -32768 (using the JPEG
 * scanning order-- 1, 8, 16, etc.), then this will produce an encoded block
 * larger than 200 bytes.
 */
const BUFSIZE: usize = vars::DCTSIZE2 * 8;

enum EncodeBufferBase<'a, const N: usize> {
    Local(&'a [u8; N]),
    Dst,
}

struct EncodeBuffer<'a, 'b, 'c, 'd, const N: usize> {
    buf: *mut u8,
    base: EncodeBufferBase<'a, N>,
    state: &'c mut HuffState,
    dst: &'d mut WorkerDst<'b>,
}

impl<'a, 'b, 'c, 'd, const N: usize> EncodeBuffer<'a, 'b, 'c, 'd, N>
where
    'a: 'd,
{
    pub fn init<'e: 'a>(
        state: &'c mut HuffState,
        dst: &'a mut WorkerDst<'b>,
        buf: &'e mut [u8; N],
    ) -> Self {
        if dst.free_in_bytes < N as u16 {
            EncodeBuffer {
                buf: buf.as_mut_ptr(),
                base: EncodeBufferBase::Local(buf),
                state,
                dst,
            }
        } else {
            EncodeBuffer {
                buf: dst.dst,
                base: EncodeBufferBase::Dst,
                state,
                dst,
            }
        }
    }

    pub fn store(self) {
        match self.base {
            EncodeBufferBase::Local(buf) => {
                let len = unsafe { self.buf.sub_ptr(buf.as_ptr()) };
                self.dst
                    .write_bytes(unsafe { slice::from_raw_parts(buf.as_ptr(), len) });
            }
            EncodeBufferBase::Dst => unsafe { self.dst.advance_to(self.buf) },
        }
    }

    pub unsafe fn EMIT_BYTE(&mut self, b: u8) {
        *self.buf = b;
        *(self.buf.add(1)) = 0;
        self.buf = self.buf.add(2 - (b < 0xFF) as usize);
    }

    unsafe fn FLUSH(&mut self) {
        if self.state.c & 0x80808080 & !(self.state.c + 0x01010101) > 0 {
            self.EMIT_BYTE((self.state.c >> 24) as u8);
            self.EMIT_BYTE((self.state.c >> 16) as u8);
            self.EMIT_BYTE((self.state.c >> 8) as u8);
            self.EMIT_BYTE(self.state.c as u8);
        } else {
            *self.buf = (self.state.c >> 24) as u8;
            *self.buf.add(1) = (self.state.c >> 16) as u8;
            *self.buf.add(2) = (self.state.c >> 8) as u8;
            *self.buf.add(3) = (self.state.c) as u8;
            self.buf = self.buf.add(4);
        }
    }

    unsafe fn PUT_AND_FLUSH(&mut self, code: usize, size: u8) {
        self.state.c = (self.state.c << (size as isize + self.state.free_bits))
            | (code >> -self.state.free_bits);
        self.FLUSH();
        self.state.free_bits += BIT_BUF_SIZE as isize;
        self.state.c = code;
    }

    pub unsafe fn PUT_BITS(&mut self, code: usize, size: u8) {
        self.state.free_bits -= size as isize;
        if self.state.free_bits < 0 {
            self.PUT_AND_FLUSH(code, size);
        } else {
            self.state.c = (self.state.c << size) | code;
        }
    }

    pub unsafe fn PUT_CODE(&mut self, code: usize, size: u8, temp: &mut i16, nbits: &mut i16) {
        *temp &= (1 << *nbits) - 1;
        *temp |= (code as i16) << *nbits;
        *nbits += size as i16;
        self.PUT_BITS(*temp as usize, *nbits as u8);
    }
}

fn JPEG_NBITS_NONZERO(x: i16) -> u8 {
    (mem::size_of_val(&x) * 8 - x.leading_zeros() as usize) as u8
}

fn JPEG_NBITS(x: i16) -> u8 {
    if x > 0 {
        JPEG_NBITS_NONZERO(x)
    } else {
        0
    }
}

impl<'a> JpegWorker<'a> {
    pub fn encode<'b, F, G, H>(
        &'a mut self,
        dst: WorkerDst<'b>,
        src: &[u8],
        pre_progress: F,
        progress: G,
    ) where
        F: FnOnce(u8) -> H,
        G: FnMut(),
        H: FnMut(),
    {
        JpegEncode { worker: self, dst }.encode(src, pre_progress, progress);
    }

    pub fn init(
        shared: &'a JpegShared,
        bufs: &'a mut WorkerBufs,
        info: &'a CInfo,
        tid: ThreadId,
    ) -> Self {
        JpegWorker {
            shared,
            bufs,
            info,
            threadId: tid,
            huffState: const_default(),
            last_dc_val: const_default(),
        }
    }
}

impl<'a, 'b, 'c> JpegEncode<'a, 'b, 'c> {
    fn write_marker(&mut self, mark: u8)
    /* Emit a marker code */
    {
        self.write_byte(0xFF);
        self.write_byte(mark);
    }

    fn write_byte(&mut self, value: u8) {
        self.dst.write_bytes(&[value]);
    }

    fn write_2bytes(&mut self, value: u16)
    /* Emit a 2-byte integer; these are always MSB first in JPEG files */
    {
        self.write_byte(((value >> 8) & 0xFF) as u8);
        self.write_byte((value & 0xFF) as u8);
    }

    fn write_jfif_app0(&mut self)
    /* Emit a JFIF-compliant APP0 marker */
    {
        /*
         * Length of APP0 block       (2 bytes)
         * Block ID                   (4 bytes - ASCII "JFIF")
         * Zero byte                  (1 byte to terminate the ID string)
         * Version Major, Minor       (2 bytes - major first)
         * Units                      (1 byte - 0x00 = none, 0x01 = inch, 0x02 = cm)
         * Xdpu                       (2 bytes - dots per unit horizontal)
         * Ydpu                       (2 bytes - dots per unit vertical)
         * Thumbnail X size           (1 byte)
         * Thumbnail Y size           (1 byte)
         */

        self.write_marker(M_APP0);

        self.write_2bytes(2 + 4 + 1 + 2 + 1 + 2 + 2 + 1 + 1); /* length */

        self.write_byte(0x4A); /* Identifier: ASCII "JFIF" */
        self.write_byte(0x46);
        self.write_byte(0x49);
        self.write_byte(0x46);
        self.write_byte(0);
        self.write_byte(1); /* Version fields */
        self.write_byte(1);
        self.write_byte(0); /* Pixel size information */
        self.write_2bytes(1);
        self.write_2bytes(1);
        self.write_byte(0); /* No thumbnail image */
        self.write_byte(0);
    }

    fn write_dqt(&mut self, index: usize)
    /* Emit a DQT marker */
    /* Returns the precision used (0 = 8bits, 1 = 16bits) for baseline checking */
    {
        let qtbl = &self.worker.shared.quantTbls.quantTbls[index];

        self.write_marker(M_DQT);
        self.write_2bytes((vars::DCTSIZE2 + 1 + 2) as u16);
        self.write_byte(index as u8);
        for i in 0..vars::DCTSIZE2 {
            /* The table entries must be emitted in zigzag order. */
            let qval = qtbl.quantval[jpeg_natural_order[i] as usize];
            self.write_byte(qval);
        }
    }

    fn write_sof(&mut self, code: u8) {
        self.write_marker(code);

        self.write_2bytes((3 * vars::MAX_COMPONENTS + 2 + 5 + 1) as u16); /* length */

        self.write_byte(8);
        self.write_2bytes(self.screen_height() as u16);
        self.write_2bytes(vars::GSP_SCREEN_WIDTH as u16);

        self.write_byte(vars::MAX_COMPONENTS as u8);

        for info in &self.worker.shared.compInfos.infos {
            self.write_byte(info.component_id);
            self.write_byte((info.h_samp_factor << 4) + info.v_samp_factor);
            self.write_byte(info.quant_tbl_no);
        }
    }

    fn screen_height(&self) -> u32 {
        if self.worker.info.isTop {
            GSP_SCREEN_HEIGHT_TOP
        } else {
            GSP_SCREEN_HEIGHT_BOTTOM
        }
    }

    fn write_dht(&mut self, mut index: usize, is_ac: bool) {
        let tbl = if is_ac {
            &self.worker.shared.huffTbls.acHuffTbls[index]
        } else {
            &self.worker.shared.huffTbls.dcHuffTbls[index]
        };
        if is_ac {
            index |= 0x10; /* output index has AC bit set */
        }

        self.write_marker(M_DHT);

        let mut length = 0 as u16;
        for i in 1..=16 {
            length += tbl.bits[i as usize] as u16;
        }

        self.write_2bytes((length + 2 + 1 + 16) as u16);
        self.write_byte(index as u8);

        for i in 1..=16 {
            self.write_byte(tbl.bits[i as usize]);
        }

        for i in 0..length {
            self.write_byte(tbl.huffVal[i as usize]);
        }
    }

    fn write_dri(&mut self) {
        self.write_marker(M_DRI);
        self.write_2bytes(4); /* fixed length */
        self.write_2bytes(self.worker.info.restartInterval);
    }

    fn write_sos(&mut self) {
        self.write_marker(M_SOS);

        self.write_2bytes((2 * vars::MAX_COMPONENTS + 2 + 1 + 3) as u16); /* length */

        self.write_byte(vars::MAX_COMPONENTS as u8);

        for i in 0..vars::MAX_COMPONENTS {
            let comp = &self.worker.shared.compInfos.infos[i];
            self.write_byte(comp.component_id);

            /* We emit 0 for unused field(s); this is recommended by the P&M text
             * but does not seem to be specified in the standard.
             */

            /* DC needs no table for refinement scan */
            let td = comp.dc_tbl_no;
            /* AC needs no table when not present */
            let ta = comp.ac_tbl_no;

            self.write_byte((td << 4) + ta);
        }

        self.write_byte(0);
        self.write_byte((vars::DCTSIZE2 - 1) as u8);
        self.write_byte(0);
    }

    fn write_headers(&mut self) {
        /* File header */
        self.write_marker(M_SOI);
        self.write_jfif_app0();

        /* Frame header */
        for i in 0..NUM_QUANT_TBLS {
            self.write_dqt(i as usize);
        }
        self.write_sof(M_SOF0);

        /* Scan header */
        for i in 0..NUM_HUFF_TBLS {
            self.write_dht(i as usize, false);
            self.write_dht(i as usize, true);
        }
        self.write_dri();
        self.write_sos();
    }

    fn write_rst(&mut self) {
        self.write_marker(vars::JPEG_RST0 + self.worker.threadId.get() as u8);
    }

    fn write_trailer(&mut self) {
        self.write_marker(M_EOI);
    }

    fn write_term(&mut self) {
        self.dst.term();
    }

    fn get_bpp_for_format(&self) -> u8 {
        match self.worker.info.colorSpace {
            ColorSpace::XBGR => 4,
            ColorSpace::BGR => 3,
            _ => 2,
        }
    }

    fn color_convert(&mut self, input: &[&[u8]; vars::MAX_SAMP_FACTOR]) {
        match self.worker.info.colorSpace {
            ColorSpace::XBGR => cconvert::<3, 2, 1, 4, { vars::MAX_SAMP_FACTOR }>(
                input,
                &mut self.worker.bufs.color,
                &self.worker.shared.colorConvTbls.rgb_ycc_tab,
            ),
            ColorSpace::BGR => cconvert::<2, 1, 0, 3, { vars::MAX_SAMP_FACTOR }>(
                input,
                &mut self.worker.bufs.color,
                &self.worker.shared.colorConvTbls.rgb_ycc_tab,
            ),
            ColorSpace::RGB565 => cconvert2::<{ vars::MAX_SAMP_FACTOR }, _>(
                input,
                rgb565_comps,
                &mut self.worker.bufs.color,
                &self.worker.shared.colorConvTbls,
            ),
            ColorSpace::RGB5A1 => cconvert2::<{ vars::MAX_SAMP_FACTOR }, _>(
                input,
                rgb5a1_comps,
                &mut self.worker.bufs.color,
                &self.worker.shared.colorConvTbls,
            ),
            ColorSpace::RGB4 => todo!(),
        }
    }

    fn fullsize_downsample(
        input: &[[u8; vars::GSP_SCREEN_WIDTH]; vars::MAX_SAMP_FACTOR],
        output: &mut [[u8; vars::GSP_SCREEN_WIDTH]; vars::MAX_SAMP_FACTOR],
    ) {
        *output = *input;
    }

    fn h2v2_downsample(
        input: &[[u8; vars::GSP_SCREEN_WIDTH]; vars::MAX_SAMP_FACTOR],
        output: &mut [u8; vars::GSP_SCREEN_WIDTH],
    ) {
        let [input0, input1] = input;
        let input0 = input0.array_chunks::<{ vars::MAX_SAMP_FACTOR }>();
        let input1 = input1.array_chunks::<{ vars::MAX_SAMP_FACTOR }>();
        let mut bias = 1;

        for ((input0, input1), output) in input0.zip(input1).zip(output) {
            *output = ((input0[0] as u16
                + input0[1] as u16
                + input1[0] as u16
                + input1[1] as u16
                + bias as u16)
                >> 2) as u8;
            bias ^= 3; /* 1=>2, 2=>1 */
        }
    }

    fn downsample<const N: usize>(&mut self, output_base: usize) {
        for (ci, comp) in (&self.worker.shared.compInfos.infos)
            .into_iter()
            .enumerate()
        {
            let input = &self.worker.bufs.color[ci];
            let output = &mut self.worker.bufs.prep[ci]
                .split_at_mut(output_base * comp.v_samp_factor as usize)
                .1
                .split_at_mut(comp.v_samp_factor as usize)
                .0;
            if comp.v_samp_factor < vars::MAX_SAMP_FACTOR as u8 {
                Self::h2v2_downsample(input, &mut output[0]);
            } else {
                Self::fullsize_downsample(input, (*output).try_into().unwrap());
            }
        }
    }

    fn pre_process(&mut self, src: [&[u8]; in_rows_blk_half], which_half: bool) {
        for (base, chunk) in src.array_chunks::<{ vars::MAX_SAMP_FACTOR }>().enumerate() {
            self.color_convert(chunk);
            self.downsample::<{ vars::MAX_SAMP_FACTOR }>(if which_half {
                base + in_rows_blk_half
            } else {
                base
            });
        }
    }

    fn convsamp(
        input: &[[u8; vars::GSP_SCREEN_WIDTH]; vars::MAX_SAMP_FACTOR * vars::DCTSIZE],
        ypos: u8,
        xpos: u8,
        output: &mut JBlock,
    ) {
        if ypos as usize > input.len() - vars::DCTSIZE
            || xpos as usize > input[0].len() - vars::DCTSIZE
        {
            panic!();
        };

        let mut oidx = 0;
        for yidx in 0..vars::DCTSIZE {
            let input = input[ypos as usize + yidx];
            for xidx in 0..vars::DCTSIZE {
                output[oidx] =
                    (input[xpos as usize + xidx] - vars::CENTERJSAMPLE as u8) as i8 as i16;

                oidx += 1;
            }
        }
    }

    fn fdct_ifast(inout: &mut JBlock) {
        const CONST_BITS: u8 = 8;

        const FIX_0_382683433: i32 = 98; /* FIX(0.382683433) */
        const FIX_0_541196100: i32 = 139; /* FIX(0.541196100) */
        const FIX_0_707106781: i32 = 181; /* FIX(0.707106781) */
        const FIX_1_306562965: i32 = 334; /* FIX(1.306562965) */

        fn MULTIPLY(v: i16, c: i32) -> i16 {
            ((v as i32 * c) >> CONST_BITS) as i16
        }

        /* Pass 1: process rows. */

        for i in (0..vars::DCTSIZE2).step_by(DCTSIZE) {
            let tmp0 = inout[i + 0] + inout[i + 7];
            let tmp7 = inout[i + 0] - inout[i + 7];
            let tmp1 = inout[i + 1] + inout[i + 6];
            let tmp6 = inout[i + 1] - inout[i + 6];
            let tmp2 = inout[i + 2] + inout[i + 5];
            let tmp5 = inout[i + 2] - inout[i + 5];
            let tmp3 = inout[i + 3] + inout[i + 4];
            let tmp4 = inout[i + 3] - inout[i + 4];

            /* Even part */

            let tmp10 = tmp0 + tmp3; /* phase 2 */
            let tmp13 = tmp0 - tmp3;
            let tmp11 = tmp1 + tmp2;
            let tmp12 = tmp1 - tmp2;

            inout[i + 0] = tmp10 + tmp11; /* phase 3 */
            inout[i + 4] = tmp10 - tmp11;

            let z1 = MULTIPLY(tmp12 + tmp13, FIX_0_707106781); /* c4 */
            inout[i + 2] = tmp13 + z1; /* phase 5 */
            inout[i + 6] = tmp13 - z1;

            /* Odd part */

            let tmp10 = tmp4 + tmp5; /* phase 2 */
            let tmp11 = tmp5 + tmp6;
            let tmp12 = tmp6 + tmp7;

            /* The rotator is modified from fig 4-8 to avoid extra negations. */
            let z5 = MULTIPLY(tmp10 - tmp12, FIX_0_382683433); /* c6 */
            let z2 = MULTIPLY(tmp10, FIX_0_541196100) + z5; /* c2-c6 */
            let z4 = MULTIPLY(tmp12, FIX_1_306562965) + z5; /* c2+c6 */
            let z3 = MULTIPLY(tmp11, FIX_0_707106781); /* c4 */

            let z11 = tmp7 + z3; /* phase 5 */
            let z13 = tmp7 - z3;

            inout[i + 5] = z13 + z2; /* phase 6 */
            inout[i + 3] = z13 - z2;
            inout[i + 1] = z11 + z4;
            inout[i + 7] = z11 - z4;
        }

        /* Pass 2: process columns. */

        for i in 0..vars::DCTSIZE {
            let tmp0 = inout[i + DCTSIZE * 0] + inout[i + DCTSIZE * 7];
            let tmp7 = inout[i + DCTSIZE * 0] - inout[i + DCTSIZE * 7];
            let tmp1 = inout[i + DCTSIZE * 1] + inout[i + DCTSIZE * 6];
            let tmp6 = inout[i + DCTSIZE * 1] - inout[i + DCTSIZE * 6];
            let tmp2 = inout[i + DCTSIZE * 2] + inout[i + DCTSIZE * 5];
            let tmp5 = inout[i + DCTSIZE * 2] - inout[i + DCTSIZE * 5];
            let tmp3 = inout[i + DCTSIZE * 3] + inout[i + DCTSIZE * 4];
            let tmp4 = inout[i + DCTSIZE * 3] - inout[i + DCTSIZE * 4];

            /* Even part */

            let tmp10 = tmp0 + tmp3; /* phase 2 */
            let tmp13 = tmp0 - tmp3;
            let tmp11 = tmp1 + tmp2;
            let tmp12 = tmp1 - tmp2;

            inout[i + DCTSIZE * 0] = tmp10 + tmp11; /* phase 3 */
            inout[i + DCTSIZE * 4] = tmp10 - tmp11;

            let z1 = MULTIPLY(tmp12 + tmp13, FIX_0_707106781); /* c4 */
            inout[i + DCTSIZE * 2] = tmp13 + z1; /* phase 5 */
            inout[i + DCTSIZE * 6] = tmp13 - z1;

            /* Odd part */

            let tmp10 = tmp4 + tmp5; /* phase 2 */
            let tmp11 = tmp5 + tmp6;
            let tmp12 = tmp6 + tmp7;

            /* The rotator is modified from fig 4-8 to avoid extra negations. */
            let z5 = MULTIPLY(tmp10 - tmp12, FIX_0_382683433); /* c6 */
            let z2 = MULTIPLY(tmp10, FIX_0_541196100) + z5; /* c2-c6 */
            let z4 = MULTIPLY(tmp12, FIX_1_306562965) + z5; /* c2+c6 */
            let z3 = MULTIPLY(tmp11, FIX_0_707106781); /* c4 */

            let z11 = tmp7 + z3; /* phase 5 */
            let z13 = tmp7 - z3;

            inout[i + DCTSIZE * 5] = z13 + z2; /* phase 6 */
            inout[i + DCTSIZE * 3] = z13 - z2;
            inout[i + DCTSIZE * 1] = z11 + z4;
            inout[i + DCTSIZE * 7] = z11 - z4;
        }
    }

    fn quantize(inout: &mut JBlock, divisors: &[[i16; vars::DCTSIZE2]; 3]) {
        for i in 0..vars::DCTSIZE2 {
            let mut temp = inout[i] as i16;
            let recip = divisors[0][i];
            let corr = divisors[1][i];
            let shift = divisors[2][i];

            if temp < 0 {
                temp = -temp;
                let mut product = (temp as u32 + corr as u32) * recip as u32;
                product >>= shift as usize + mem::size_of::<i16>() * 8;
                temp = product as i16;
                temp = -temp;
            } else {
                let mut product = (temp as u32 + corr as u32) * recip as u32;
                product >>= shift as usize + mem::size_of::<i16>() * 8;
                temp = product as i16;
            }
            inout[i] = temp;
        }
    }

    fn forward_DCT(
        input: &[[u8; vars::GSP_SCREEN_WIDTH]; vars::MAX_SAMP_FACTOR * vars::DCTSIZE],
        output: &mut JBlock,
        ypos: u8,
        xpos: u8,
        divisors: &[[i16; vars::DCTSIZE2]; 3],
    ) {
        Self::convsamp(input, ypos, xpos, output);
        Self::fdct_ifast(output);
        Self::quantize(output, divisors);
    }

    fn compress(&mut self, MCU_col_num: u8) {
        let mut blkn = 0;

        if MCU_col_num > MCUs_per_row {
            panic!();
        }

        for ci in 0..vars::MAX_COMPONENTS {
            let comp = &self.worker.shared.compInfos.infos[ci];
            let MCU_width = comp.h_samp_factor;
            let MCU_height = comp.v_samp_factor;

            if MCU_width > vars::MAX_SAMP_FACTOR as u8 || MCU_height > vars::MAX_SAMP_FACTOR as u8 {
                panic!();
            }

            let MCU_sample_width = MCU_width * vars::DCTSIZE as u8;
            let xpos = MCU_col_num * MCU_sample_width;
            let mut ypos = 0;

            for _ in 0..MCU_height {
                let mut xpos = xpos;
                for _ in 0..MCU_width {
                    Self::forward_DCT(
                        &self.worker.bufs.prep[ci],
                        &mut self.worker.bufs.mcu[blkn as usize],
                        ypos,
                        xpos,
                        &self.worker.shared.divisors.divisors[comp.quant_tbl_no as usize],
                    );

                    xpos += vars::DCTSIZE as u8;
                    blkn += 1;
                }
                ypos += vars::DCTSIZE as u8;
            }
        }
    }

    fn encode_one_block(
        dst: &mut WorkerDst,
        state: &mut HuffState,
        block: &mut [i16; vars::DCTSIZE2],
        last_dc_val: i16,
        dc_derived_tbl: &DerivedTbl,
        ac_derived_tbl: &DerivedTbl,
    ) {
        let mut localbuf: [u8; BUFSIZE] = const_default();
        let mut buf = EncodeBuffer::init(state, dst, &mut localbuf);

        let mut temp = block[0] - last_dc_val;
        let mut nbits = temp >> (core::mem::size_of_val(&temp) * 8 - 1);
        temp += nbits;
        nbits ^= temp;

        nbits = JPEG_NBITS(nbits) as i16;
        unsafe {
            buf.PUT_CODE(
                dc_derived_tbl.ehufco[nbits as usize] as usize,
                dc_derived_tbl.ehufsi[nbits as usize],
                &mut temp,
                &mut nbits,
            )
        };

        let mut r = 0;

        for jpeg_natural_order_of_k in jpeg_natural_order.into_iter().skip(1) {
            temp = block[jpeg_natural_order_of_k as usize];
            if temp == 0 {
                r += 16;
            } else {
                nbits = temp >> (core::mem::size_of_val(&temp) * 8 - 1);
                temp += nbits;
                nbits ^= temp;

                nbits = JPEG_NBITS_NONZERO(nbits) as i16;

                while r >= 16 * 16 {
                    r -= 16 * 16;
                    unsafe {
                        buf.PUT_BITS(
                            ac_derived_tbl.ehufco[0xf0] as usize,
                            ac_derived_tbl.ehufsi[0xf0],
                        )
                    };
                }
                r += nbits;
                unsafe {
                    buf.PUT_CODE(
                        ac_derived_tbl.ehufco[r as usize] as usize,
                        ac_derived_tbl.ehufsi[r as usize],
                        &mut temp,
                        &mut nbits,
                    )
                };
                r = 0;
            }
        }

        if r > 0 {
            unsafe { buf.PUT_BITS(ac_derived_tbl.ehufco[0] as usize, ac_derived_tbl.ehufsi[0]) };
        }

        buf.store();
    }

    fn encode_mcu(&mut self) {
        let mut blkn = 0;

        for ci in 0..vars::MAX_COMPONENTS {
            let comp = &self.worker.shared.compInfos.infos[ci];
            let MCU_width = comp.h_samp_factor;
            let MCU_height = comp.v_samp_factor;

            for _ in 0..MCU_height {
                for _ in 0..MCU_width {
                    let last_dc_val = self.worker.last_dc_val[ci];
                    Self::encode_one_block(
                        &mut self.dst,
                        &mut self.worker.huffState,
                        &mut self.worker.bufs.mcu[blkn],
                        last_dc_val,
                        &self.worker.shared.entropyTbls.dc_derived_tbls[comp.dc_tbl_no as usize],
                        &self.worker.shared.entropyTbls.ac_derived_tbls[comp.ac_tbl_no as usize],
                    );
                    self.worker.last_dc_val[ci] = self.worker.bufs.mcu[blkn][0];

                    blkn += 1;
                }
            }
        }
    }

    fn reset_mcu(&mut self) {
        self.worker.huffState = const_default();
        self.worker.huffState.free_bits = BIT_BUF_SIZE as isize;
        self.worker.last_dc_val = const_default();
    }

    fn flush_mcu(&mut self) {
        let mut put_bits = BIT_BUF_SIZE as isize - self.worker.huffState.free_bits;

        let mut localbuf: [u8; mem::size_of::<BitBufType>()] = const_default();
        let put_buffer = self.worker.huffState.c;
        let mut buf = EncodeBuffer::init(&mut self.worker.huffState, &mut self.dst, &mut localbuf);

        while put_bits >= 8 {
            put_bits -= 8;
            let temp = put_buffer >> put_bits;
            unsafe { buf.EMIT_BYTE(temp as u8) }
        }
        if put_bits > 0 {
            /* fill partial byte with ones */
            let temp = (put_buffer << (8 - put_bits)) | (0xFF >> put_bits);
            unsafe { buf.EMIT_BYTE(temp as u8) }
        }

        buf.store()
    }

    fn process(&mut self) {
        for MCU_col_num in 0..MCUs_per_row {
            self.compress(MCU_col_num);
            self.encode_mcu();
        }
    }

    pub fn encode<F, G, H>(&mut self, src: &[u8], pre_progress: F, mut progress: G)
    where
        F: FnOnce(u8) -> H,
        G: FnMut(),
        H: FnMut(),
    {
        let bpp = self.get_bpp_for_format();
        let pitch = vars::GSP_SCREEN_WIDTH * bpp as usize;

        if src.len() != pitch * self.screen_height() as usize {
            panic!();
        }

        let src_chunks = src.chunks_exact(pitch);
        let mut pre_progress = pre_progress(src_chunks.len() as u8);

        pre_progress();

        if self.worker.threadId.get() == 0 {
            self.write_headers();
        }

        self.reset_mcu();
        for chunks in src_chunks.array_chunks::<in_rows_blk>() {
            /* Pre-process */
            let mut chunks = chunks.array_chunks::<in_rows_blk_half>();

            let chunk0 = chunks.next().unwrap();
            self.pre_process(*chunk0, false);

            let chunk1 = chunks.next().unwrap();
            self.pre_process(*chunk1, true);

            pre_progress();

            /* Compress and encode */
            self.process();

            progress();
        }
        self.flush_mcu();

        if self.worker.threadId.get() == self.worker.shared.coreCount.get() - 1 {
            self.write_trailer();
        } else {
            self.write_rst();
        }

        self.write_term();
    }
}
