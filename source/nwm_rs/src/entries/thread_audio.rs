use crate::*;
use core::sync::atomic::{AtomicU32, Ordering};

// NTR-HR+ game-audio capture (new-3ds only): read the dsp final mix through
// the physical+0x80000000 mirror and stream it as audio packets via rp_output.
// note: a thread entry must end via svcExitThread, the initial lr is null.

const DSP_REGION0: u32 = 0x1FF50000 + 0x80000000; // final-mix region 0 (mirror VA)
const DSP_REGION1: u32 = 0x1FF70000 + 0x80000000; // final-mix region 1 (mirror VA)
const DSP_FINAL_SAMPLES_OFF: u32 = 0xA80; // (0x8540 - 0x8000) DSP words * 2
const DSP_FRAME_COUNTER_OFF: u32 = 0x7FFE; // last u16 of the 0x8000-byte region

const AUDIO_SAMPLE_RATE: u32 = 32728; // dsp final-mix rate
const AUDIO_FRAME_SAMPLES: u32 = 160; // samples per dsp mix frame
const AUDIO_FRAME_BYTES: usize = 640; // 160 samples * 2 ch * 2 bytes (s16 LE)
const AUDIO_HDR_TYPE: u8 = 4; // hdr[2]: NTR-HR+ audio packet type
const AUDIO_FMT_PCM16: u8 = 0; // hdr[3]: payload format/version

// each packet carries the previous and current frame so one lost packet
// leaves no gap; hdr[0] is the newest frame's sequence number
const AUDIO_REDUNDANCY: usize = 2;
const AUDIO_PAYLOAD_BYTES: usize = AUDIO_REDUNDANCY * AUDIO_FRAME_BYTES;

// poll well under the ~4.888 ms dsp frame so no mix frame is missed
pub const AUDIO_POLL_NS: s64 =
    1_000_000_000 * AUDIO_FRAME_SAMPLES as s64 / AUDIO_SAMPLE_RATE as s64 / 3;

// fresher region by frame_counter (wrap-safe)
fn newer_region(fc0: u16, fc1: u16) -> u32 {
    if fc1 != fc0 && fc1.wrapping_sub(fc0) < 0x8000 {
        DSP_REGION1
    } else {
        DSP_REGION0
    }
}

pub const AUDIO_QOS_BUDGET: u32 =
    (AUDIO_SAMPLE_RATE / AUDIO_FRAME_SAMPLES) * (DATA_HDR_SIZE as u32 + AUDIO_PAYLOAD_BYTES as u32);

// spsc ring: the audio thread (core 1) produces packets, the nwm thread
// (core 2) drains and sends them, so nwmSendPacket stays single-threaded and
// no cross-core send lock is needed. length must stay a power of two.
const AUDIO_Q_LEN: usize = 8;
const AUDIO_PKT_SIZE: usize = DATA_HDR_SIZE as usize + AUDIO_PAYLOAD_BYTES;

struct AudioWorkArea {
    staging: [u8; AUDIO_PKT_SIZE],
    bufs: [[u8; NWM_PACKET_SIZE as usize]; AUDIO_Q_LEN],
    pool: mp_pool_t,
    head: AtomicU32, // consumer index (nwm thread)
    tail: AtomicU32, // producer index (audio thread)
}

static mut AUDIO_WORK: *mut AudioWorkArea = const_default();

pub fn once_audio() {
    if let Some(b) = request_mem_from_pool::<{ mem::size_of::<AudioWorkArea>() }>() {
        unsafe {
            AUDIO_WORK = b.to_ptr() as *mut AudioWorkArea;
            let packet_buf = (*AUDIO_WORK).staging.as_mut_ptr();
            *packet_buf.add(1) = 0; // hdr[1]: flags (reserved)
            *packet_buf.add(2) = AUDIO_HDR_TYPE; // hdr[2]: audio type
            *packet_buf.add(3) = AUDIO_FMT_PCM16; // hdr[3]: format/version
        }
    }
}

pub static mut AUDIO_ENABLE: bool = false;

#[named]
pub fn init() -> bool {
    unsafe {
        if AUDIO_WORK.is_null() {
            return false;
        }
        let w = &mut *AUDIO_WORK;
        if mp_init(
            (*w.bufs.as_ptr()).len(),
            w.bufs.len(),
            w.bufs.as_mut_ptr().as_mut_ptr() as *mut _,
            &mut w.pool,
        ) < 0
        {
            ns_dbg_print!(mp_init_failed, c_str!("AUDIO_WORK.pool"));
            return false;
        }
        w.head.store(0, Ordering::Relaxed);
        w.tail.store(0, Ordering::Relaxed);
        true
    }
}

// hand the assembled packet to the nwm thread; drop if the ring is full
// (audio is best-effort, the viewer fills gaps from the redundant frame)
unsafe fn audio_enqueue(pkt: *const u8) {
    let w = unsafe { &mut *AUDIO_WORK };

    let head = w.head.load(Ordering::Acquire);
    let tail = w.tail.load(Ordering::Relaxed);
    if tail.wrapping_sub(head) as usize >= AUDIO_Q_LEN {
        return;
    }
    let buf = w.bufs[tail as usize & (AUDIO_Q_LEN - 1)].as_mut_ptr();
    unsafe { ptr::copy_nonoverlapping(pkt, buf.add(NWM_HDR_SIZE as usize), AUDIO_PKT_SIZE) };
    w.tail.store(tail.wrapping_add(1), Ordering::Release);
}

// drained by the nwm thread each loop iteration, so only it calls nwmSendPacket;
// a dead session drains without sending so the ring cannot back up forever
pub fn drain_audio() {
    let ready = unsafe { AUDIO_ENABLE }
        && entries::thread_nwm::nwm_send_ready()
        && entries::thread_nwm::nwm_session_alive();
    loop {
        let w = unsafe { &mut *AUDIO_WORK };
        let tail = w.tail.load(Ordering::Acquire);
        let head = w.head.load(Ordering::Relaxed);
        if head == tail {
            break;
        }
        let buf = w.bufs[head as usize & (AUDIO_Q_LEN - 1)].as_mut_ptr();
        if ready {
            let packet_buf = unsafe { buf.add(NWM_HDR_SIZE as usize) };

            let curr_tick = get_system_tick().get() as u32;
            let mut next_tick = unsafe { entries::thread_nwm::RP_OUTPUT_NEXT_TICK };
            let tick_diff = next_tick as s32 - curr_tick as s32;

            if tick_diff > 0 {
                let sleep_value = DurationTick::init(tick_diff as s64).get_ns();
                sleep_thread(sleep_value);

                if NWM_AGGRESSIVE_NEXT_TICK == 0 {
                    next_tick = get_system_tick().get() as u32
                }
            } else {
                next_tick = curr_tick;
            }

            let _ =
                unsafe { entries::thread_nwm::rp_output(packet_buf, AUDIO_PKT_SIZE, next_tick) };
        }
        w.head.store(head.wrapping_add(1), Ordering::Release);
    }
}

pub extern "C" fn thread_audio(_: *mut c_void) {
    unsafe {
        __system_initSyscalls();
    }

    let packet_buf = unsafe { (*AUDIO_WORK).staging.as_mut_ptr() };
    // zeroed so the first packet's older slot is silence
    let payload = unsafe { packet_buf.add(DATA_HDR_SIZE as usize) };
    unsafe { ptr::write_bytes(payload, 0, AUDIO_PAYLOAD_BYTES) };
    let newest = unsafe { payload.add((AUDIO_REDUNDANCY - 1) * AUDIO_FRAME_BYTES) };

    let mut seq: u8 = 0;
    let mut last_fc: u16 = 0;
    let mut have_last = false;

    while !reset_threads() {
        let fc0 =
            unsafe { ptr::read_volatile((DSP_REGION0 + DSP_FRAME_COUNTER_OFF) as *const u16) };
        let fc1 =
            unsafe { ptr::read_volatile((DSP_REGION1 + DSP_FRAME_COUNTER_OFF) as *const u16) };
        let src = newer_region(fc0, fc1);
        let fc = if src == DSP_REGION1 { fc1 } else { fc0 };

        // skip if the mix hasn't advanced
        if have_last && fc == last_fc {
            unsafe { svcSleepThread(AUDIO_POLL_NS) };
            continue;
        }
        last_fc = fc;
        have_last = true;

        unsafe {
            // drop the oldest frame, read the new one into the last slot
            ptr::copy(
                payload.add(AUDIO_FRAME_BYTES),
                payload,
                (AUDIO_REDUNDANCY - 1) * AUDIO_FRAME_BYTES,
            );
            ptr::copy_nonoverlapping(
                (src + DSP_FINAL_SAMPLES_OFF) as *const u8,
                newest,
                AUDIO_FRAME_BYTES,
            );
            *packet_buf.add(0) = seq; // hdr[0]: newest frame's sequence number
            // hand off to the nwm thread; it owns every nwmSendPacket call
            audio_enqueue(packet_buf);
        }
        seq = seq.wrapping_add(1);

        unsafe { svcSleepThread(AUDIO_POLL_NS) };
    }

    unsafe { svcExitThread() }
}
