#![allow(dead_code)]

use crate::*;
mod vars;
use vars::*;

#[derive(ConstDefault)]
struct JpegShared {
    huffTbls: HuffTbls,
    entropyTbls: EntropyTbls,
    colorConvTbls: ColorConvTabs,
    compInfos: CompInfos,
    quantTbls: QuantTbls,
    divisors: Divisors,
}

#[derive(Default)]
pub struct WorkerDst<'a> {
    dst: core::slice::IterMut<'a, u8>,
}

impl<'a> WorkerDst<'a> {
    fn writeByte(&mut self, b: u8) {
        loop {
            match self.dst.next() {
                Some(dst) => {
                    *dst = b;
                    return;
                }
                None => self.flush(),
            }
        }
    }
    fn flush(&mut self) {
        todo!()
    }
    fn term(&mut self) {
        todo!()
    }
}

#[derive(ConstDefault)]
pub struct CInfo {
    isTop: bool,
    colorSpace: ColorSpace,
    restartInterval: u16,
    workIndex: WorkIndex,
}

#[derive(ConstDefault)]
pub struct JpegWorker<'a, 'b> {
    shared: &'a JpegShared,
    bufs: &'a mut WorkerBufs,
    dst: WorkerDst<'b>,
    info: &'a CInfo,
    threadId: ThreadId,
}

type JpegWorkers<'a, 'b> = [JpegWorker<'a, 'b>; RP_CORE_COUNT_MAX as usize];

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

    pub fn reset<'a, 'b>(&'a mut self, quality: u32) -> JpegWorkers<'a, 'b> {
        self.shared.quantTbls.setQuality(quality);
        self.shared.divisors.setDivisors(&self.shared.quantTbls);

        let [b0, b1, b2] = &mut self.bufs;
        [
            JpegWorker {
                shared: &self.shared,
                bufs: b0,
                dst: Default::default(),
                info: &self.info,
                threadId: ThreadId::init_val(0),
            },
            JpegWorker {
                shared: &self.shared,
                bufs: b1,
                dst: Default::default(),
                info: &self.info,
                threadId: ThreadId::init_val(1),
            },
            JpegWorker {
                shared: &self.shared,
                bufs: b2,
                dst: Default::default(),
                info: &self.info,
                threadId: ThreadId::init_val(2),
            },
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

impl<'a, 'b> JpegWorker<'a, 'b> {
    pub fn setDst<'c: 'b>(&mut self, dst: WorkerDst<'c>) {
        self.dst = dst;
    }

    fn write_marker(&mut self, mark: u8)
    /* Emit a marker code */
    {
        self.write_byte(0xFF);
        self.write_byte(mark);
    }

    fn write_byte(&mut self, value: u8) {
        self.dst.writeByte(value);
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
        let qtbl = &self.shared.quantTbls.quantTbls[index];

        self.write_marker(M_DQT);
        self.write_2bytes((DCTSIZE2 + 1 + 2) as u16);
        self.write_byte(index as u8);
        for i in 0..DCTSIZE2 {
            /* The table entries must be emitted in zigzag order. */
            let qval = qtbl.quantval[jpeg_natural_order[i as usize] as usize];
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

        for info in &self.shared.compInfos.infos {
            self.write_byte(info.component_id);
            self.write_byte((info.h_samp_factor << 4) + info.v_samp_factor);
            self.write_byte(info.quant_tbl_no);
        }
    }

    fn screen_height(&self) -> u32 {
        if self.info.isTop {
            GSP_SCREEN_HEIGHT_TOP
        } else {
            GSP_SCREEN_HEIGHT_BOTTOM
        }
    }

    fn write_dht(&mut self, mut index: usize, is_ac: bool) {
        let tbl = if is_ac {
            &self.shared.huffTbls.acHuffTbls[index]
        } else {
            &self.shared.huffTbls.dcHuffTbls[index]
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
        self.write_2bytes(self.info.restartInterval);
    }

    fn write_sos(&mut self) {
        self.write_marker(M_SOS);

        self.write_2bytes((2 * vars::MAX_COMPONENTS + 2 + 1 + 3) as u16); /* length */

        self.write_byte(vars::MAX_COMPONENTS as u8);

        for i in 0..vars::MAX_COMPONENTS {
            let comp = &self.shared.compInfos.infos[i];
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
        self.write_byte((DCTSIZE2 - 1) as u8);
        self.write_byte(0);
    }

    pub fn write_headers(&mut self) {
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

    pub fn write_rst(&mut self) {
        self.write_marker(vars::JPEG_RST0 + self.threadId.get() as u8);
    }

    pub fn write_trailer(&mut self) {
        self.write_marker(M_EOI);
    }

    pub fn write_term(&mut self) {
        self.dst.term();
    }

    fn get_bpp_for_format(&self) -> u8 {
        match self.info.colorSpace {
            ColorSpace::XBGR => 4,
            ColorSpace::BGR => 3,
            _ => 2,
        }
    }

    fn color_convert(&mut self, input: &[&[u8]; vars::MAX_SAMP_FACTOR]) {
        match self.info.colorSpace {
            ColorSpace::XBGR => cconvert::<3, 2, 1, 4, { vars::MAX_SAMP_FACTOR }>(
                input,
                &mut self.bufs.color,
                &self.shared.colorConvTbls.rgb_ycc_tab,
            ),
            ColorSpace::BGR => cconvert::<2, 1, 0, 3, { vars::MAX_SAMP_FACTOR }>(
                input,
                &mut self.bufs.color,
                &self.shared.colorConvTbls.rgb_ycc_tab,
            ),
            ColorSpace::RGB565 => cconvert2::<{ vars::MAX_SAMP_FACTOR }, _>(
                input,
                rgb565_comps,
                &mut self.bufs.color,
                &self.shared.colorConvTbls,
            ),
            ColorSpace::RGB5A1 => cconvert2::<{ vars::MAX_SAMP_FACTOR }, _>(
                input,
                rgb5a1_comps,
                &mut self.bufs.color,
                &self.shared.colorConvTbls,
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
        for (ci, comp) in (&self.shared.compInfos.infos).into_iter().enumerate() {
            let input = &self.bufs.color[ci];
            let output = &mut self.bufs.prep[ci]
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

    fn process(&mut self) {
        todo!()
    }

    pub fn encode<F, G, H>(&mut self, src: &[u8], pre_progress: F, mut progress: G)
    where
        F: FnOnce(u8) -> H,
        G: FnMut(),
        H: FnMut(),
    {
        let bpp = self.get_bpp_for_format();
        let pitch = vars::GSP_SCREEN_WIDTH * bpp as usize;
        let src_chunks = src.chunks_exact(pitch);
        let mut pre_progress = pre_progress(src_chunks.len() as u8);

        pre_progress();

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
    }
}
