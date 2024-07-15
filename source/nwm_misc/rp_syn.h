#ifndef RP_SYN_H
#define RP_SYN_H

#include "global.h"

typedef Handle rp_lock_t;
typedef Handle rp_sem_t;

#define rp_lock_init(n) svcCreateMutex(&n, 0)
#define rp_lock_wait_try(n) svcWaitSynchronization(n, 0)
#define rp_lock_wait(n, to) svcWaitSynchronization(n, to)
#define rp_lock_rel(n) svcReleaseMutex(n)
#define rp_lock_close(n) do { if (n) svcCloseHandle(n); } while (0)

#define rp_sem_init(n, i, m) svcCreateSemaphore(&n, i, m)
#define rp_sem_wait_try(n) svcWaitSynchronization(n, 0)
#define rp_sem_wait(n, to) svcWaitSynchronization(n, to)
#define rp_sem_rel(n, c) do { s32 count; svcReleaseSemaphore(&count, n, c); } while (0)
#define rp_sem_close(n) do { if (n) svcCloseHandle(n); } while (0)

struct rp_syn_comp_func_t {
	rp_sem_t sem;
	rp_lock_t mutex;
	u8 pos_head, pos_tail;
	u8 count;
	void **pos;
};

u8 rp_atomic_fetch_addb_wrap(u8 *p, u8 a, u8 factor);
int rp_syn_init1(struct rp_syn_comp_func_t *syn1, int init, void *base, u32 stride, int count, void **pos);
int rp_syn_acq(struct rp_syn_comp_func_t *syn1, s64 timeout, void **pos);
void rp_syn_rel(struct rp_syn_comp_func_t *syn1, void *pos);
int rp_syn_acq1(struct rp_syn_comp_func_t *syn1, s64 timeout, void **pos);
int rp_syn_rel1(struct rp_syn_comp_func_t *syn1, void *pos);

#endif
