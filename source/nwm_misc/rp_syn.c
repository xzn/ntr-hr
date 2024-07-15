#include "rp_syn.h"
#include "3ds/result.h"

u8 rp_atomic_fetch_addb_wrap(u8 *p, u8 a, u8 factor) {
	u8 v, v_new;
	do {
		v = __atomic_load_n(p, __ATOMIC_ACQUIRE);
		v_new = (v + a) % factor;
	} while (!__atomic_compare_exchange_n(p, &v, v_new, 1, __ATOMIC_RELEASE, __ATOMIC_RELAXED));
	return v;
}

int rp_syn_init1(struct rp_syn_comp_func_t *syn1, int init, void *base, u32 stride, int count, void **pos) {
	int res;
	rp_sem_close(syn1->sem);
	if ((res = rp_sem_init(syn1->sem, init ? count : 0, count)))
		return res;
	rp_lock_close(syn1->mutex);
	if ((res = rp_lock_init(syn1->mutex)))
		return res;

	syn1->pos_head = syn1->pos_tail = 0;
	syn1->count = count;
	syn1->pos = pos;

	for (int i = 0; i < count; ++i) {
		syn1->pos[i] = init ? ((u8 *)base) + i * stride : 0;
	}
	return 0;
}

int rp_syn_acq(struct rp_syn_comp_func_t *syn1, s64 timeout, void **pos) {
	int res;
	if ((res = rp_sem_wait(syn1->sem, timeout)) != 0) {
		if (R_DESCRIPTION(res) != RD_TIMEOUT)
			nsDbgPrint("rp_syn_acq wait sem error: %d %d %d %d\n",
				R_LEVEL(res), R_SUMMARY(res), R_MODULE(res), R_DESCRIPTION(res));
		return res;
	}
	u8 pos_tail = syn1->pos_tail;
	syn1->pos_tail = (pos_tail + 1) % syn1->count;
	*pos = syn1->pos[pos_tail];
	syn1->pos[pos_tail] = 0;
	if (!*pos) {
		nsDbgPrint("error rp_syn_acq at pos %d\n", pos_tail);
		return -1;
	}
	return 0;
}

void rp_syn_rel(struct rp_syn_comp_func_t *syn1, void *pos) {
	u8 pos_head = syn1->pos_head;
	syn1->pos_head = (pos_head + 1) % syn1->count;
	syn1->pos[pos_head] = pos;
	rp_sem_rel(syn1->sem, 1);
}

int rp_syn_acq1(struct rp_syn_comp_func_t *syn1, s64 timeout, void **pos) {
	int res;
	if ((res = rp_sem_wait(syn1->sem, timeout)) != 0) {
		if (R_DESCRIPTION(res) != RD_TIMEOUT)
			nsDbgPrint("rp_syn_acq wait sem error: %d %d %d %d\n",
				R_LEVEL(res), R_SUMMARY(res), R_MODULE(res), R_DESCRIPTION(res));
		return res;
	}
	u8 pos_tail = rp_atomic_fetch_addb_wrap(&syn1->pos_tail, 1, syn1->count);
	*pos = syn1->pos[pos_tail];
	syn1->pos[pos_tail] = 0;
	if (!*pos) {
		nsDbgPrint("error rp_syn_acq at pos %d\n", pos_tail);
		return -1;
	}
	return 0;
}

int rp_syn_rel1(struct rp_syn_comp_func_t *syn1, void *pos) {
	int res;
	if ((res = rp_lock_wait(syn1->mutex, NWM_THREAD_WAIT_NS))) {
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
