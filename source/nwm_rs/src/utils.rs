use crate::*;

pub const fn htons(v: u16) -> u16 {
    v.to_be()
}

#[allow(unused)]
pub const fn ntohs(v: u16) -> u16 {
    v.swap_bytes()
}

pub struct MemRegionBase<B, const N: usize>(mem::MaybeUninit<[B; N]>);

impl<B, const N: usize> MemRegionBase<B, N> {
    pub fn to_ptr(&mut self) -> *mut B {
        unsafe { self.0.assume_init_mut().as_mut_ptr() }
    }
}

pub type MemRegion8<const N: usize> = MemRegionBase<u8, N>;

impl<const N: usize> MemRegion8<N> {
    unsafe fn from_ptr<'a>(p: *mut u8) -> &'a mut Self {
        unsafe { mem::transmute(p) }
    }
}

pub fn request_mem_from_pool<const N: usize>() -> Option<&'static mut MemRegion8<N>> {
    let s = unsafe { plgRequestMemory(N as u32) };
    if s > 0 {
        let t = unsafe { MemRegion8::<N>::from_ptr(s as *mut u8) };
        Some(t)
    } else {
        None
    }
}

#[cfg(not(feature = "mem3"))]
pub fn request_mem_from_pool_vsize(t: usize) -> Option<&'static mut [u8]> {
    let s = unsafe { plgRequestMemory(t as u32) };
    if s > 0 {
        let t = unsafe { slice::from_raw_parts_mut(s as *mut u8, t) };
        Some(t)
    } else {
        None
    }
}

struct StackRegionCount<const N: usize>;

impl<const N: usize> StackRegionCount<N> {
    const N: usize = N / mem::size_of::<u32>();
}

pub struct StackRegion<const N: usize>(MemRegionBase<u32, { StackRegionCount::<N>::N }>)
where
    [(); { N % mem::size_of::<u32>() == 0 } as usize - 1]:,
    [(); StackRegionCount::<N>::N]:;

pub fn stack_region_from_mem_region<'a, const N: usize>(
    m: &'a mut MemRegion8<N>,
) -> &'a mut StackRegion<N>
where
    [(); { N % mem::size_of::<u32>() == 0 } as usize - 1]:,
    [(); StackRegionCount::<N>::N]:,
{
    unsafe { mem::transmute(m) }
}

pub struct PhantomType<'a, T>(pub T, PhantomData<&'a ()>);

type PhantomResult<'a> = PhantomType<'a, Result>;

pub fn create_thread<'a, 'b: 'a, const N: usize>(
    h: &'a mut Handle,
    f: ThreadFunc,
    a: u32,
    t: &'b mut StackRegion<N>,
    prio: s32,
    core: s32,
) -> PhantomResult<'a>
where
    [(); StackRegionCount::<N>::N]:,
    [(); { N % mem::size_of::<u32>() == 0 } as usize - 1]:,
    [(); { N >= SMALL_STACK_SIZE as usize } as usize - 1]:,
{
    unsafe {
        PhantomType(
            svcCreateThread(
                h,
                f,
                a,
                t.0.to_ptr().add(StackRegionCount::<N>::N - 10),
                prio,
                core,
            ),
            PhantomData,
        )
    }
}

pub fn create_thread_from_pool<'a, const N: usize>(
    h: &'a mut Handle,
    f: ThreadFunc,
    a: u32,
    prio: s32,
    core: s32,
) -> PhantomResult<'a>
where
    [(); StackRegionCount::<N>::N]:,
    [(); { N % mem::size_of::<u32>() == 0 } as usize - 1]:,
    [(); { N >= SMALL_STACK_SIZE as usize } as usize - 1]:,
{
    if let Some(t) = request_mem_from_pool::<N>() {
        create_thread(h, f, a, stack_region_from_mem_region(t), prio, core)
    } else {
        PhantomType(-1, PhantomData)
    }
}

pub struct DurationTick(s64);

impl DurationTick {
    pub const fn init(tick: s64) -> Self {
        DurationTick(tick)
    }

    pub const fn get(&self) -> s64 {
        self.0
    }

    pub const fn get_ns(&self) -> DurationNs {
        DurationNs(self.0 * 1_000_000_000 / SYSCLOCK_ARM11 as s64)
    }
}

#[derive(ConstDefault, Clone, Copy)]
pub struct DurationNs(s64);

impl DurationNs {
    pub const fn init(ns: s64) -> Self {
        DurationNs(ns)
    }

    pub const fn get(&self) -> s64 {
        self.0
    }
}

pub fn sleep_thread(duration_ns: DurationNs) {
    unsafe { svcSleepThread(duration_ns.0) }
}

pub fn get_system_tick() -> DurationTick {
    DurationTick(unsafe { svcGetSystemTick() as s64 })
}

pub fn is_top_index(is_top: bool) -> ScreenIndex {
    ScreenIndex::init((if is_top { RP_SCREEN_TOP } else { RP_SCREEN_BOT }) as u32)
}

pub struct CreateThread<'a>(Handle, PhantomData<&'a ()>);

impl<'a> CreateThread<'a> {
    #[allow(unused)]
    pub fn create<'b: 'a, const N: usize>(
        f: ThreadFunc,
        a: u32,
        t: &'b mut StackRegion<N>,
        prio: s32,
        core: s32,
    ) -> Option<Self>
    where
        [(); StackRegionCount::<N>::N]:,
        [(); { N % mem::size_of::<u32>() == 0 } as usize - 1]:,
        [(); { N >= SMALL_STACK_SIZE as usize } as usize - 1]:,
    {
        let mut h: Handle = 0;
        let res = create_thread(&mut h, f, a, t, prio, core);
        if res.0 != 0 {
            None
        } else {
            Some(Self(h, PhantomData))
        }
    }
}

impl<'a> Drop for CreateThread<'a> {
    fn drop(&mut self) {
        unsafe {
            let _ = svcCloseHandle(self.0);
        }
    }
}

#[allow(unused)]
pub struct JoinThread<'a>(CreateThread<'a>);

impl<'a> JoinThread<'a> {
    #[allow(unused)]
    pub fn create(t: CreateThread<'a>) -> Self {
        Self(t)
    }
}

impl<'a> Drop for JoinThread<'a> {
    fn drop(&mut self) {
        unsafe {
            let _ = svcWaitSynchronization(self.0.0, -1);
        }
    }
}

#[must_use]
pub fn create_event(h: &mut Handle) -> Result {
    unsafe { svcCreateEvent(h, RESET_ONESHOT) }
}

#[must_use]
#[allow(unused)]
pub fn wait_syn(cname: CName, h: Handle, syn_name: *const c_char) -> Option<()> {
    while !reset_threads() {
        let ret = wait_syn_ns(cname, h, syn_name, THREAD_WAIT_NS)?;
        if ret {
            return Some(());
        }
    }
    None
}

#[must_use]
#[cfg(not(feature = "o3ds"))]
pub fn wait_syn_once(cname: CName, h: Handle, syn_name: *const c_char) -> Option<bool> {
    wait_syn_ns(cname, h, syn_name, THREAD_WAIT_NS)
}

#[must_use]
pub fn wait_syn_ns(
    cname: CName,
    h: Handle,
    syn_name: *const c_char,
    dur: DurationNs,
) -> Option<bool> {
    let res = unsafe { svcWaitSynchronization(h, dur.get()) };
    if res != 0 {
        if res != RES_TIMEOUT as s32 {
            unsafe {
                ns_dbg_print_cname!(cname, wait_syn_failed, syn_name, res);
                set_reset_threads();
                svcSleepThread(THREAD_WAIT_NS.get());
            }
            return None;
        }
        return Some(false);
    }
    Some(true)
}

#[allow(unused)]
pub unsafe fn release_mutex(cname: CName, h: Handle, syn_name: *const c_char) {
    let res = unsafe { svcReleaseMutex(h) };
    if res != 0 {
        ns_dbg_print_cname!(cname, release_mutex_failed, syn_name, res);
    }
}

#[allow(unused)]
pub unsafe fn release_sem(cname: CName, h: Handle, syn_name: *const c_char) {
    unsafe { release_sem_count(cname, h, syn_name, 1) }
}

#[allow(unused)]
pub unsafe fn release_sem_count(
    cname: CName,
    h: Handle,
    syn_name: *const c_char,
    release_count: s32,
) {
    let mut count = mem::MaybeUninit::<s32>::uninit();
    let res = unsafe { svcReleaseSemaphore(count.as_mut_ptr(), h, release_count) };
    if res != 0 {
        ns_dbg_print_cname!(cname, release_sem_failed, syn_name, res);
    }
}

#[allow(unused)]
pub fn unchecked_div(a: u64, b: u64) -> u64 {
    unsafe { core::intrinsics::unchecked_div(a, b) }
}
