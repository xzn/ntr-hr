use super::*;

pub struct ThreadVars<'a>(&'a crate::entries::thread_screen::ScreenEncodeVars);

impl<'a> ThreadVars<'a> {
    pub fn work_begin_acquire(
        &'a self,
    ) -> core::result::Result<ThreadBeginVars<'a>, ThreadBeginRestVars> {
        unsafe {
            if (*syn_handles)
                .works
                .get_mut(&self.0.work_index())
                .work_begin_flag
                .swap(true, Ordering::Relaxed)
                == false
            {
                Ok(ThreadBeginVars(&self))
            } else {
                Err(ThreadBeginRestVars())
            }
        }
    }

    pub fn blit_ctx(&self) -> &mut BlitCtx {
        unsafe { blit_ctxes.get_mut(&self.0.work_index()) }
    }

    pub fn v(&self) -> &crate::entries::thread_screen::ScreenEncodeVars {
        self.0
    }

    #[named]
    pub fn release(self) {
        unsafe {
            let w = self.v().work_index();
            let syn = (*syn_handles).works.get(&w);

            let f = syn.work_done_count.fetch_add(1, Ordering::Relaxed);
            if f == 0 {
                let p = load_and_progresses.get_mut(&w);
                for j in ThreadId::up_to(&core_count_in_use) {
                    *p.p_snapshot.get_mut(&j) = p.p.get_mut(&j).load(Ordering::Relaxed);
                }
            }
            if f == core_count_in_use.get() - 1 {
                syn.work_done_count.store(0, Ordering::Relaxed);
                syn.work_begin_flag.store(false, Ordering::Relaxed);

                let mut count = mem::MaybeUninit::uninit();
                let res = svcReleaseSemaphore(count.as_mut_ptr(), syn.work_done, 1);
                if res != 0 {
                    nsDbgPrint!(releaseSemaphoreFailed, c_str!("work_done"), res);
                }
            }
        }
    }
}

pub struct ThreadBeginRestVars();

impl ThreadBeginRestVars {
    #[named]
    pub fn acquire(self, t: &ThreadId) -> bool {
        unsafe {
            loop {
                if reset_threads() {
                    return false;
                }
                let res = svcWaitSynchronization(
                    (*syn_handles).threads.get(&t).work_begin_ready,
                    THREAD_WAIT_NS,
                );
                if res != 0 {
                    if res != RES_TIMEOUT as s32 {
                        nsDbgPrint!(waitForSyncFailed, c_str!("work_begin_ready"), res);
                        svcSleepThread(THREAD_WAIT_NS);
                    }
                    continue;
                }
                return true;
            }
        }
    }
}

pub struct ThreadBeginVars<'a>(&'a ThreadVars<'a>);

impl<'a> ThreadBeginVars<'a> {
    pub fn ctx(&self) -> &mut BlitCtx {
        self.0.blit_ctx()
    }

    pub fn cinfos(&self) -> &mut CInfosThreads {
        unsafe {
            cinfos
                .get_mut(&ScreenIndex::from_bool(self.v().is_top()))
                .get_mut(&self.v().work_index())
        }
    }

    pub fn v(&self) -> &'a crate::entries::thread_screen::ScreenEncodeVars {
        &self.0.v()
    }

    pub fn frame_id(&self) -> u8_ {
        unsafe { *current_frame_ids.get_b_mut(self.v().is_top()) }
    }

    #[named]
    pub fn dma_sync(&self) {
        unsafe {
            let res = svcWaitSynchronization(self.v().dma(), THREAD_WAIT_NS);
            if res != 0 {
                if res != RES_TIMEOUT as s32 {
                    nsDbgPrint!(waitForSyncFailed, c_str!("dmas"), res);
                    svcSleepThread(THREAD_WAIT_NS);
                }
            }
        }
    }

    pub fn frame_changed(&self) -> bool {
        unsafe {
            let ctx = self.ctx();
            let src_len = ctx.width * ctx.src_pitch;
            *slice::from_raw_parts(ctx.src, src_len as usize)
                != *slice::from_raw_parts(self.v().img_src_prev(), src_len as usize)
        }
    }

    pub fn ready_next(&self) {
        unsafe {
            self.v().img_index_next();
            *current_frame_ids.get_b_mut(self.v().is_top()) += 1;
        }
    }

    #[named]
    pub fn release(self, t: &ThreadId) {
        unsafe {
            let mut count = mem::MaybeUninit::uninit();
            self.v().set_skip_frame(false);
            self.v().clear_screen_synced();

            for j in ThreadId::up_to(&core_count_in_use) {
                if j != *t {
                    let res = svcReleaseSemaphore(
                        count.as_mut_ptr(),
                        (*syn_handles).threads.get(&j).work_begin_ready,
                        1,
                    );
                    if res != 0 {
                        nsDbgPrint!(releaseSemaphoreFailed, c_str!("work_begin_ready"), res);
                    }
                }
            }
        }
    }

    #[named]
    pub fn release_skip_frame(&self, t: &ThreadId) -> bool {
        unsafe {
            let mut count = mem::MaybeUninit::uninit();
            self.v().set_skip_frame(true);
            self.v().set_screen_thread_id(&t);

            let res = svcReleaseSemaphore(count.as_mut_ptr(), (*syn_handles).screen_ready, 1);
            if res != 0 {
                nsDbgPrint!(releaseSemaphoreFailed, c_str!("screen_ready"), res);
            }

            loop {
                if reset_threads() {
                    return false;
                }

                let res = svcWaitSynchronization(
                    (*syn_handles).threads.get(&t).work_ready,
                    THREAD_WAIT_NS,
                );
                if res != 0 {
                    if res != RES_TIMEOUT as s32 {
                        nsDbgPrint!(waitForSyncFailed, c_str!("work_ready"), res);
                        svcSleepThread(THREAD_WAIT_NS);
                    }
                    continue;
                }
                break;
            }
            true
        }
    }
}

pub unsafe fn work_thread_loop(t: ThreadId) -> Option<()> {
    loop {
        let vars = crate::entries::thread_screen::screen_encode_acquire(&t)?;
        safe_impl::send_frame(&t, ThreadVars(&vars))
    }
}
