#include "rp_syn_chan.h"
#include "rp_syn.h"

int rp_screen_queue_init(struct rp_syn_comp_t *screen, struct rp_screen_encode_t *base, int count) {
	return rp_syn_init(screen, 0, 0,
		base, sizeof(struct rp_screen_encode_t), count);
}

int rp_network_queue_init(struct rp_syn_comp_t *network, struct rp_network_encode_t *base, int count) {
	return rp_syn_init(network, 1, 1,
		base, sizeof(struct rp_network_encode_t), count);
}

struct rp_screen_encode_t *rp_screen_transfer_acquire(struct rp_syn_comp_func_t *syn1, s64 timeout) {
	return rp_syn_acq(syn1, timeout);
}

void rp_screen_encode_release(struct rp_syn_comp_func_t *syn1, struct rp_screen_encode_t *pos) {
	rp_syn_rel(syn1, pos);
}

struct rp_screen_encode_t *rp_screen_encode_acquire(struct rp_syn_comp_func_t *syn1, s64 timeout) {
	return rp_syn_acq1(syn1, timeout);
}

int rp_screen_transfer_release(struct rp_syn_comp_func_t *syn1, struct rp_screen_encode_t *pos) {
	return rp_syn_rel1(syn1, pos);
}

struct rp_network_encode_t *rp_network_transfer_acquire(struct rp_syn_comp_func_t *syn1, s64 timeout) {
	return rp_syn_acq(syn1, timeout);
}

void rp_network_encode_release(struct rp_syn_comp_func_t *syn1, struct rp_network_encode_t *pos) {
	rp_syn_rel(syn1, pos);
}

struct rp_network_encode_t *rp_network_encode_acquire(struct rp_syn_comp_func_t *syn1, s64 timeout, int multicore) {
	if (multicore) {
		return rp_syn_acq1(syn1, timeout);
	} else {
		return rp_syn_acq(syn1, timeout);
	}
}

int rp_network_transfer_release(struct rp_syn_comp_func_t *syn1, struct rp_network_encode_t *pos, int multicore) {
	if (multicore)	{
		return rp_syn_rel1(syn1, pos);
	} else {
		rp_syn_rel(syn1, pos);
		return 0;
	}
}
