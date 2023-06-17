#include "rp_syn.h"

int LightLock_LockTimeout(LightLock* lock, s64 timeout)
{
	s32 val;
	bool bAlreadyLocked;

	// Try to lock, or if that's not possible, increment the number of waiting threads
	do
	{
		// Read the current lock state
		val = __ldrex(lock);
		if (val == 0) val = 1; // 0 is an invalid state - treat it as 1 (unlocked)
		bAlreadyLocked = val < 0;

		// Calculate the desired next state of the lock
		if (!bAlreadyLocked)
			val = -val; // transition into locked state
		else
			--val; // increment the number of waiting threads (which has the sign reversed during locked state)
	} while (__strex(lock, val));

	// While the lock is held by a different thread:
	while (bAlreadyLocked)
	{
		// Wait for the lock holder thread to wake us up
		Result rc;
		rc = syncArbitrateAddressWithTimeout(lock, ARBITRATION_WAIT_IF_LESS_THAN_TIMEOUT, 0, timeout);
		// if (R_DESCRIPTION(rc) == RD_TIMEOUT)
		if (rc)
		{
			do
			{
				val = __ldrex(lock);
				bAlreadyLocked = val < 0;

				if (!bAlreadyLocked)
					--val;
				else
					++val;
			} while (__strex(lock, val));

			__dmb();
			return rc;
		}

		// Try to lock again
		do
		{
			// Read the current lock state
			val = __ldrex(lock);
			bAlreadyLocked = val < 0;

			// Calculate the desired next state of the lock
			if (!bAlreadyLocked)
				val = -(val-1); // decrement the number of waiting threads *and* transition into locked state
			else
			{
				// Since the lock is still held, we need to cancel the atomic update and wait again
				__clrex();
				break;
			}
		} while (__strex(lock, val));
	}

	__dmb();
	return 0;
}

int LightSemaphore_AcquireTimeout(LightSemaphore* semaphore, s32 count, s64 timeout)
{
	s32 old_count;
	s16 num_threads_acq;

	do
	{
		for (;;)
		{
			old_count = __ldrex(&semaphore->current_count);
			if (old_count >= count)
				break;
			__clrex();

			do
				num_threads_acq = (s16)__ldrexh((u16 *)&semaphore->num_threads_acq);
			while (__strexh((u16 *)&semaphore->num_threads_acq, num_threads_acq + 1));

			Result rc;
			rc = syncArbitrateAddressWithTimeout(&semaphore->current_count, ARBITRATION_WAIT_IF_LESS_THAN_TIMEOUT, count, timeout);

			do
				num_threads_acq = (s16)__ldrexh((u16 *)&semaphore->num_threads_acq);
			while (__strexh((u16 *)&semaphore->num_threads_acq, num_threads_acq - 1));

			// if (R_DESCRIPTION(rc) == RD_TIMEOUT)
			if (rc)
			{
				__dmb();
				return rc;
			}
		}
	} while (__strex(&semaphore->current_count, old_count - count));

	__dmb();
	return 0;
}

u8 rp_atomic_fetch_addb_wrap(u8 *p, u8 a, u8 factor) {
	u8 v, v_new;
	do {
		v = __atomic_load_n(p, __ATOMIC_ACQUIRE);
		v_new = (v + a) % factor;
	} while (!__atomic_compare_exchange_n(p, &v, v_new, 1, __ATOMIC_RELEASE, __ATOMIC_RELAXED));
	return v;
}

static int rp_syn_init1(struct rp_syn_comp_func_t *syn1, int init, int id, void *base, u32 stride, int count) {
	int res;
	rp_sem_close(syn1->sem);
	if ((res = rp_sem_init(syn1->sem, init ? count : 0, count)))
		return res;
	rp_lock_close(syn1->mutex);
	if ((res = rp_lock_init(syn1->mutex)))
		return res;

	syn1->pos_head = syn1->pos_tail = 0;
	syn1->id = id;
	syn1->count = count;

	for (int i = 0; i < count; ++i) {
		syn1->pos[i] = init ? ((u8 *)base) + i * stride : 0;
	}
	return 0;
}

int rp_syn_init(struct rp_syn_comp_t *syn, int transfer_encode, int id, void *base, u32 stride, int count) {
	int res;
	if ((res = rp_syn_init1(&syn->transfer, transfer_encode == 0, id, base, stride, count)))
		return res;
	if ((res = rp_syn_init1(&syn->encode, transfer_encode == 1, id, base, stride, count)))
		return res;
	return 0;
}

void *rp_syn_acq(struct rp_syn_comp_func_t *syn1, s64 timeout) {
	Result res;
	if ((res = rp_sem_wait(syn1->sem, timeout)) != 0) {
		if (R_DESCRIPTION(res) != RD_TIMEOUT)
			nsDbgPrint("rp_syn_acq wait sem error: %d %d %d %d\n",
				R_LEVEL(res), R_SUMMARY(res), R_MODULE(res), R_DESCRIPTION(res));
		return 0;
	}
	u8 pos_tail = syn1->pos_tail;
	syn1->pos_tail = (pos_tail + 1) % syn1->count;
	void *pos = syn1->pos[pos_tail];
	syn1->pos[pos_tail] = 0;
	if (!pos) {
		nsDbgPrint("error rp_syn_acq id %d at pos %d\n", syn1->id, pos_tail);
		return 0;
	}
	return pos;
}

void rp_syn_rel(struct rp_syn_comp_func_t *syn1, void *pos) {
	u8 pos_head = syn1->pos_head;
	syn1->pos_head = (pos_head + 1) % syn1->count;
	syn1->pos[pos_head] = pos;
	rp_sem_rel(syn1->sem, 1);
}

void *rp_syn_acq1(struct rp_syn_comp_func_t *syn1, s64 timeout) {
	Result res;
	if ((res = rp_sem_wait(syn1->sem, timeout)) != 0) {
		if (R_DESCRIPTION(res) != RD_TIMEOUT)
			nsDbgPrint("rp_syn_acq wait sem error: %d %d %d %d\n",
				R_LEVEL(res), R_SUMMARY(res), R_MODULE(res), R_DESCRIPTION(res));
		return 0;
	}
	u8 pos_tail = rp_atomic_fetch_addb_wrap(&syn1->pos_tail, 1, syn1->count);
	void *pos = syn1->pos[pos_tail];
	syn1->pos[pos_tail] = 0;
	if (!pos) {
		nsDbgPrint("error rp_syn_acq id %d at pos %d\n", syn1->id, pos_tail);
		return 0;
	}
	return pos;
}

int rp_syn_rel1(struct rp_syn_comp_func_t *syn1, void *pos) {
	int res;
	if ((res = rp_lock_wait(syn1->mutex, RP_SYN_WAIT_MAX))) {
		if (R_DESCRIPTION(res) != RD_TIMEOUT)
			nsDbgPrint("rp_syn_rel1 wait mutex error: %d %d %d %d\n",
				R_LEVEL(res), R_SUMMARY(res), R_MODULE(res), R_DESCRIPTION(res));
		return res;
	}

	u8 pos_head = syn1->pos_head;
	syn1->pos_head = (pos_head + 1) % syn1->count;
	syn1->pos[pos_head] = pos;
	rp_lock_rel(syn1->mutex);
	rp_sem_rel(syn1->sem, 1);
	return 0;
}
