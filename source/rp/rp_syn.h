#ifndef RP_SYN_H
#define RP_SYN_H

#include "rp_common.h"

#if (RP_SYN_METHOD == 0)
typedef Handle rp_lock_t;
typedef Handle rp_sem_t;

#define rp_lock_init(n) svc_createMutex(&n, 0)
#define rp_lock_wait_try(n) svc_waitSynchronization1(n, 0)
#define rp_lock_wait(n, to) svc_waitSynchronization1(n, to)
#define rp_lock_rel(n) svc_releaseMutex(n)
#define rp_lock_close(n) do { if (n) svc_closeHandle(n); } while (0)

#define rp_sem_init(n, i, m) svc_createSemaphore(&n, i, m)
#define rp_sem_wait_try(n) svc_waitSynchronization1(n, 0)
#define rp_sem_wait(n, to) svc_waitSynchronization1(n, to)
#define rp_sem_rel(n, c) do { s32 count; svc_releaseSemaphore(&count, n, c); } while (0)
#define rp_sem_close(n) do { if (n) svc_closeHandle(n); } while (0)
#else
typedef LightLock rp_lock_t;
typedef LightSemaphore rp_sem_t;

#define rp_lock_init(n) (LightLock_Init(&n), 0)
#define rp_lock_wait_try(n) (-LightLock_TryLock(&n))
#define rp_lock_wait(n, to) LightLock_LockTimeout(&n, to)
#define rp_lock_rel(n) LightLock_Unlock(&n)
#define rp_lock_close(n) ((void)0)

#define rp_sem_init(n, i, m) (LightSemaphore_Init(&n, i, m), 0)
#define rp_sem_wait_try(n) (-LightSemaphore_TryAcquire(&n, 1))
#define rp_sem_wait(n, to) LightSemaphore_AcquireTimeout(&n, 1, to)
#define rp_sem_rel(n, c) LightSemaphore_Release(&n, c)
#define rp_sem_close(n) ((void)0)
#endif

struct rp_syn_comp_t {
	struct rp_syn_comp_func_t {
		u8 id;
		rp_sem_t sem;
		rp_lock_t mutex;
		u8 pos_head, pos_tail;
		void *pos[RP_ENCODE_BUFFER_MAX_COUNT];
		u8 count;
	} transfer, encode;
};

int LightLock_LockTimeout(LightLock* lock, s64 timeout);
int LightSemaphore_AcquireTimeout(LightSemaphore* semaphore, s32 count, s64 timeout);
u8 rp_atomic_fetch_addb_wrap(u8 *p, u8 a, u8 factor);
int rp_syn_init(struct rp_syn_comp_t *syn, int transfer_encode, int id, void *base, u32 stride, int count);
void *rp_syn_acq(struct rp_syn_comp_func_t *syn1, s64 timeout);
void rp_syn_rel(struct rp_syn_comp_func_t *syn1, void *pos);
void *rp_syn_acq1(struct rp_syn_comp_func_t *syn1, s64 timeout);
int rp_syn_rel1(struct rp_syn_comp_func_t *syn1, void *pos);

#endif
