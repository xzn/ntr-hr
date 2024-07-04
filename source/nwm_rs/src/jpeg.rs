#![allow(dead_code)]

use crate::*;
mod vars;
use vars::*;

#[derive(ConstDefault)]
struct JpegShared {
    huffTbls: HuffTbls,
    entropyTbls: EntropyTbls,
    colorConvTbls: ColorConvTabs,
    quantTbls: QuantTbls,
    divisors: Divisors,
}

pub struct Jpeg {
    private: (),
    shared: JpegShared,
}

pub struct JpegWorker<'a> {
    private: (),
    shared: &'a JpegShared,
}

impl Jpeg {
    pub fn init() -> Self {
        let mut shared: JpegShared = const_default();
        shared.huffTbls = HuffTbls::init();
        setEntropyTbls(&mut shared.entropyTbls, &shared.huffTbls);
        shared.colorConvTbls = ColorConvTabs::init();

        Jpeg {
            private: (),
            shared,
        }
    }

    pub fn reset<'a, 'b: 'a>(
        &'b mut self,
        quality: u32,
    ) -> [JpegWorker<'a>; RP_CORE_COUNT_MAX as usize] {
        setQuality(&mut self.shared.quantTbls, quality);
        setDivisors(&mut self.shared.divisors, &self.shared.quantTbls);

        [
            JpegWorker {
                private: (),
                shared: &self.shared,
            },
            JpegWorker {
                private: (),
                shared: &self.shared,
            },
            JpegWorker {
                private: (),
                shared: &self.shared,
            },
        ]
    }
}

impl<'a> JpegWorker<'a> {
    pub fn ready(&mut self) {}
    pub fn encode(&mut self) {}
}
