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

        self.write_2bytes((3 * MAX_COMPONENTS + 2 + 5 + 1) as u16); /* length */

        self.write_byte(8);
        self.write_2bytes(self.screen_height() as u16);
        self.write_2bytes(GSP_SCREEN_WIDTH as u16);

        self.write_byte(MAX_COMPONENTS as u8);

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

        self.write_2bytes((2 * MAX_COMPONENTS + 2 + 1 + 3) as u16); /* length */

        self.write_byte(MAX_COMPONENTS as u8);

        for i in 0..MAX_COMPONENTS {
            let comp = &self.shared.compInfos.infos[i as usize];
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
}
