use crate::*;

pub const fn htons(v: u16_) -> u16_ {
    v.to_be()
}

pub const fn align_to_page_size(s: usize) -> usize {
    if s == 0 {
        0
    } else {
        ((s - 1) / 0x1000 + 1) * 0x1000
    }
}

pub struct OpenProcess(Handle);

impl OpenProcess {
    #[named]
    pub fn open(pid: u32_) -> Option<Self> {
        let mut h = mem::MaybeUninit::uninit();
        let res = unsafe { svcOpenProcess(h.as_mut_ptr(), pid) };

        if R_SUCCEEDED(res) {
            let h = unsafe { h.assume_init() };
            Some(Self(h))
        } else {
            nsDbgPrint!(openProcessFailed, res);
            None
        }
    }

    pub fn handle(&self) -> Handle {
        self.0
    }
}

impl Drop for OpenProcess {
    fn drop(&mut self) {
        unsafe {
            let _ = svcCloseHandle(self.0);
        }
    }
}

pub struct MemRegionBase<B, const T: usize>(mem::MaybeUninit<[B; T]>);

impl<B, const T: usize> MemRegionBase<B, T> {
    pub fn to_ptr(&mut self) -> *mut B {
        unsafe { mem::transmute::<&mut Self, *mut B>(self) }
    }
}

pub type MemRegion8<const T: usize> = MemRegionBase<u8_, T>;

impl<const T: usize> MemRegion8<T> {
    unsafe fn from_ptr<'a>(p: *mut u8_) -> &'a mut Self {
        mem::transmute::<*mut u8_, &mut MemRegion8<T>>(p)
    }
}

pub struct StackRegionCount<const T: usize>;

impl<const T: usize> StackRegionCount<T> {
    const N: usize = T / mem::size_of::<u32_>();
}

pub struct StackRegionSize<const N: usize>;

impl<const N: usize> StackRegionSize<N> {
    const T: usize = N * mem::size_of::<u32_>();
}

pub type StackRegion<const T: usize> = MemRegionBase<u32_, { StackRegionCount::<T>::N }>;

pub fn stack_region_from_mem_region<'a, const T: usize>(
    m: &'a mut MemRegion8<T>,
) -> &'a mut StackRegion<T>
where
    [(); StackRegionCount::<T>::N]:,
{
    unsafe { mem::transmute::<&mut MemRegion8<T>, &mut StackRegion<T>>(m) }
}

pub fn request_mem_from_pool<const T: usize>() -> Option<&'static mut MemRegion8<T>> {
    let s = unsafe { plgRequestMemory(T as u32_) };
    if s > 0 {
        let t = unsafe { MemRegion8::<T>::from_ptr(s as *mut u8_) };
        Some(t)
    } else {
        None
    }
}

pub fn create_thread<const T: usize>(
    h: *mut Handle,
    f: ThreadFunc,
    a: u32_,
    t: &mut StackRegion<T>,
    prio: s32,
    core: s32,
) -> Result
where
    [(); StackRegionCount::<T>::N]:,
{
    unsafe {
        svcCreateThread(
            h,
            f,
            a,
            t.to_ptr().add(StackRegionCount::<T>::N),
            prio,
            core,
        )
    }
}

pub struct CreateThread(Handle);

impl CreateThread {
    pub fn create<const T: usize>(
        f: ThreadFunc,
        a: u32_,
        t: &mut StackRegion<T>,
        prio: s32,
        core: s32,
    ) -> Option<Self>
    where
        [(); StackRegionCount::<T>::N]:,
    {
        let mut h = mem::MaybeUninit::<Handle>::uninit();
        if R_FAILED(create_thread(h.as_mut_ptr(), f, a, t, prio, core)) {
            None
        } else {
            let h = unsafe { h.assume_init() };
            Some(Self(h))
        }
    }
}

impl Drop for CreateThread {
    fn drop(&mut self) {
        unsafe {
            let _ = svcCloseHandle(self.0);
        }
    }
}

pub struct JoinThread(CreateThread);

impl JoinThread {
    pub fn create(t: CreateThread) -> Self {
        Self(t)
    }
}

impl Drop for JoinThread {
    fn drop(&mut self) {
        unsafe {
            let _ = svcWaitSynchronization(self.0 .0, -1);
        }
    }
}

pub fn create_thread_from_pool<const T: usize>(
    h: *mut Handle,
    f: ThreadFunc,
    a: u32_,
    prio: s32,
    core: s32,
) -> Result
where
    [(); StackRegionCount::<T>::N]:,
{
    if let Some(t) = request_mem_from_pool::<T>() {
        create_thread(h, f, a, stack_region_from_mem_region(t), prio, core)
    } else {
        -1
    }
}
