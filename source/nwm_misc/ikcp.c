//=====================================================================
//
// KCP - A Better ARQ Protocol Implementation
// skywind3000 (at) gmail.com, 2010-2011
//
// Features:
// + Average RTT reduce 30% - 40% vs traditional ARQ like tcp.
// + Maximum RTT reduce three times vs tcp.
// + Lightweight, distributed as a single source file.
//
//=====================================================================
#include "ikcp.h"
#include "constants.h"

#include <stddef.h>
#include <string.h>

#include "3ds/os.h"

#pragma GCC diagnostic ignored "-Wunused-parameter"
#pragma GCC diagnostic ignored "-Wunused-function"
#pragma GCC diagnostic ignored "-Wunused-variable"

FecalEncoder rp_kcp_fecal_encoder;

enum FEC_TYPE {
	FEC_TYPE_1_1,
	FEC_TYPE_1_2,
	FEC_TYPE_1_3,
	FEC_TYPE_2_3,
	FEC_TYPE_COUNT,
};


_Static_assert(FEC_TYPE_COUNT <= (1 << FTY_NBITS));

struct fec_counts_t {
	IUINT8 original_count, recovery_count;
};

static const struct fec_counts_t FEC_COUNTS[] = {
	{1, 0},
	{1, 1},
	{1, 2},
	{2, 1},
};

_Static_assert(sizeof(FEC_COUNTS) / sizeof(*FEC_COUNTS) == FEC_TYPE_COUNT);

static int fec_send_intervals[FEC_TYPE_COUNT];

// max of original_count + recovery_count
#define FEC_COUNT_MAX (1 << GID_NBITS)

//=====================================================================
// KCP BASIC
//=====================================================================

// static struct SavedSeg {
// 	IUINT16 pid; // packet id
// 	IUINT16 fid; // fec packet group id
// 	IUINT8 fty; // fec type
// 	IUINT8 gid; // id within fec packet group
// 	IUINT8 wrn; // wait resend count
// } saved_segs[1 << PID_NBITS];

// #define TIMING_INFO

#ifdef TIMING_INFO
static u64 saved_timing;
static u64 last_timing;
static u64 max_timing;
static IUINT16 saved_fid;
#endif


//---------------------------------------------------------------------
// encode / decode
//---------------------------------------------------------------------

/* encode 8 bits unsigned int */
static inline char *ikcp_encode8u(char *p, unsigned char c)
{
	*(unsigned char*)p++ = c;
	return p;
}

/* decode 8 bits unsigned int */
static inline char *ikcp_decode8u(char *p, unsigned char *c)
{
	*c = *(unsigned char*)p++;
	return p;
}

/* encode 16 bits unsigned int (lsb) */
static inline char *ikcp_encode16u(char *p, unsigned short w)
{
#if IWORDS_BIG_ENDIAN || IWORDS_MUST_ALIGN
	*(unsigned char*)(p + 0) = (w & 255);
	*(unsigned char*)(p + 1) = (w >> 8);
#else
	memcpy(p, &w, 2);
#endif
	p += 2;
	return p;
}

/* decode 16 bits unsigned int (lsb) */
static inline char *ikcp_decode16u(char *p, unsigned short *w)
{
#if IWORDS_BIG_ENDIAN || IWORDS_MUST_ALIGN
	*w = *(unsigned char*)(p + 1);
	*w = *(unsigned char*)(p + 0) + (*w << 8);
#else
	memcpy(w, p, 2);
#endif
	p += 2;
	return p;
}

/* encode 32 bits unsigned int (lsb) */
static inline char *ikcp_encode32u(char *p, IUINT32 l)
{
#if IWORDS_BIG_ENDIAN || IWORDS_MUST_ALIGN
	*(unsigned char*)(p + 0) = (unsigned char)((l >>  0) & 0xff);
	*(unsigned char*)(p + 1) = (unsigned char)((l >>  8) & 0xff);
	*(unsigned char*)(p + 2) = (unsigned char)((l >> 16) & 0xff);
	*(unsigned char*)(p + 3) = (unsigned char)((l >> 24) & 0xff);
#else
	memcpy(p, &l, 4);
#endif
	p += 4;
	return p;
}

/* decode 32 bits unsigned int (lsb) */
static inline char *ikcp_decode32u(char *p, IUINT32 *l)
{
#if IWORDS_BIG_ENDIAN || IWORDS_MUST_ALIGN
	*l = *(unsigned char*)(p + 3);
	*l = *(unsigned char*)(p + 2) + (*l << 8);
	*l = *(unsigned char*)(p + 1) + (*l << 8);
	*l = *(unsigned char*)(p + 0) + (*l << 8);
#else
	memcpy(l, p, 4);
#endif
	p += 4;
	return p;
}

static inline IUINT32 _imin_(IUINT32 a, IUINT32 b) {
	return a <= b ? a : b;
}

static inline IUINT32 _imax_(IUINT32 a, IUINT32 b) {
	return a >= b ? a : b;
}

static inline IUINT32 _ibound_(IUINT32 lower, IUINT32 middle, IUINT32 upper)
{
	return _imin_(_imax_(lower, middle), upper);
}

static inline IINT32 _itimediff(IUINT32 later, IUINT32 earlier)
{
	return ((IINT32)(later - earlier));
}

//---------------------------------------------------------------------
// manage segment
//---------------------------------------------------------------------
typedef struct IKCPSEG IKCPSEG;

// allocate a new kcp segment
static IKCPSEG* ikcp_segment_malloc(ikcpcb *kcp)
{
	return (IKCPSEG*)mp_malloc(&kcp->seg_pool);
}

// delete a segment
static void ikcp_segment_free(ikcpcb *kcp, IKCPSEG *seg)
{
	if (!seg->weak_data) {
		if (seg->is_kcp_seg_data) {
			ikcp_seg_data_buf_free(seg->data_buf);
		} else if (seg->is_term_seg_data) {
			rp_term_data_buf_free(seg->data_buf);
		} else {
			rp_seg_data_buf_free(seg->data_buf);
		}
	}
	mp_free(&kcp->seg_pool, seg);
}

// output segment
static int ikcp_output(ikcpcb *kcp, void *data, int size)
{
	if (size == 0) return -1;
	return rp_udp_output(data, size, kcp);
}


//---------------------------------------------------------------------
// create a new kcpcb
//---------------------------------------------------------------------
int ikcp_create(ikcpcb* kcp, IUINT16 cid)
{
	*kcp = (struct IKCPCB){ 0 };
	kcp->cid = cid & ((1 << CID_NBITS) - 1);

	iqueue_init(&kcp->snd_lst);
	for (int i = 0; i < RSND_COUNT; ++i) {
		iqueue_init(&kcp->rsnd_lsts[i]);
	}
	iqueue_init(&kcp->snd_cur);
	iqueue_init(&kcp->snd_wak);
	kcp->n_snd_max = ARQ_BUFS_COUNT;
	kcp->n_cur_max = ARQ_CUR_BUFS_COUNT;

	int ret;

	ret = mp_init(
		ARQ_SEG_SIZE,
		ARQ_SEG_MEM_COUNT,
		kcp->seg_mem,
		&kcp->seg_pool);
	if (ret != 0)
		return -1;

	// memset(saved_segs, 0, sizeof(saved_segs));
#ifdef TIMING_INFO
	saved_timing = 0;
	max_timing = 0;
	last_timing = svcGetSystemTick();
	saved_fid = (IUINT16)-1;
#endif
#ifdef CHECK_PID
	rp_arq_bitset_clear_all(&kcp->pid_bs);
#endif

	return 0;
}

#define pid_bs_offset (1 << (PID_NBITS - 1))
#define pid_bs_n (1 << (PID_NBITS - 2))

// static IUINT8 data_counter = 0;

static char *ikcp_get_fec_data_buf(char *data_buf) {
	return data_buf - (ARQ_OVERHEAD_SIZE - FEC_OVERHEAD_SIZE);
}

static char *ikcp_get_packet_data_buf(char *data_buf) {
	return data_buf - ARQ_OVERHEAD_SIZE;
}

//---------------------------------------------------------------------
// user/upper level send
//---------------------------------------------------------------------
int ikcp_queue(ikcpcb *kcp, char *buffer, int len)
{
	IUINT32 size = len;
	bool term = size & (1u << 31);
	bool notify = size & (1u << 30);
	size &= ~0 & ~(1u << 31) & ~(1u << 30);
	len = size;

	if (len != ARQ_DATA_SIZE) {
		// nsDbgPrint("%d", len);
		return -2;
	}

	if (ikcp_queue_get_free(kcp) <= 0 ) {
		return -4;
	}

#ifdef CHECK_PID
	if (rp_arq_bitset_check_n_wrapped(&kcp->pid_bs, (kcp->pid + pid_bs_offset) & ((1 << PID_NBITS) - 1), pid_bs_n)) {
		// nsDbgPrint("rp_arq_bitset_check_n_wrapped for %d ffs %d", (int)kcp->pid, (int)rp_arq_bitset_ffs_n_wrapped(&kcp->pid_bs, (kcp->pid + pid_bs_offset) & ((1 << PID_NBITS) - 1), pid_bs_n));
		return 1;
	}

	if (rp_arq_bitset_check(&kcp->pid_bs, kcp->pid)) {
		nsDbgPrint("rp_arq_bitset_check failed for %d", (int)kcp->pid);
		return -3;
	}
#endif

	IKCPSEG *seg = ikcp_segment_malloc(kcp);
	if (seg == NULL) {
		return -3;
	}

	// memset(ikcp_get_fec_data_buf(buffer) + sizeof(IUINT16), data_counter, FEC_DATA_SIZE - sizeof(IUINT16));
	// ++data_counter;

	*seg = (struct IKCPSEG){ 0 };
	seg->data_buf = buffer;
	seg->pid = kcp->pid;
	seg->is_term_seg_data = term;
	seg->term_notify = notify;
#ifdef CHECK_PID
	rp_arq_bitset_set(&kcp->pid_bs, kcp->pid);
#endif
	// nsDbgPrint("rp_arq_bitset_set for %d", (int)kcp->pid);
	++kcp->pid;
	kcp->pid &= ((1 << PID_NBITS) - 1);

	iqueue_init(&seg->node);
	iqueue_add_tail(&seg->node, &kcp->snd_lst);
	++kcp->n_snd;

	return 0;
}


//---------------------------------------------------------------------
// parse ack
//---------------------------------------------------------------------


//---------------------------------------------------------------------
// ack append
//---------------------------------------------------------------------


//---------------------------------------------------------------------
// parse data
//---------------------------------------------------------------------


//---------------------------------------------------------------------
// input data
//---------------------------------------------------------------------

enum ARQ_QUEUE {
	ARQ_QUEUE_RSND2,
	ARQ_QUEUE_RSND1,
	ARQ_QUEUE_RSND0,
	ARQ_QUEUE_SND,
	ARQ_QUEUE_COUNT,
};

_Static_assert(ARQ_QUEUE_COUNT == RSND_COUNT + 1); // rsnd count + snd
_Static_assert(ARQ_QUEUE_RSND0 == RSND_COUNT - 1);

static struct IQUEUEHEAD *arq_queue_get(ikcpcb *kcp, enum ARQ_QUEUE queue) {
	if (queue <= ARQ_QUEUE_RSND0) {
		return &kcp->rsnd_lsts[ARQ_QUEUE_RSND0 - queue];
	} else if (queue == ARQ_QUEUE_SND) {
		return &kcp->snd_lst;
	} else {
		return 0;
	}
}

static struct IQUEUEHEAD *arq_queue_get_from_wrn(ikcpcb *kcp, int wrn) {
	if (wrn > 0) {
		wrn = _imin_(wrn, RSND_COUNT) - 1;
		return &kcp->rsnd_lsts[wrn];
	}
	return &kcp->snd_lst;
}

// FIXME:
// Currently works on little endian only

static int ikcp_input_check_nack(IUINT16 pid, ikcpcb *kcp) {
	for (int i = 0; i < kcp->n_nacks; ++i) {
		IUINT16 nack_start = kcp->nacks[i][0];
		IUINT16 pid_diff = (pid - nack_start) & ((1 << PID_NBITS) - 1);
		if (pid_diff >= (1 << PID_NBITS) - (1 << (PID_NBITS - 2))) {
			return 0;
		}
		IUINT16 nack_count_0 = kcp->nacks[i][1];
		if (pid_diff <= nack_count_0) {
			return -1;
		}
	}

	return 0;
}

static int ikcp_input_handle_send_wak_nack(ikcpcb *kcp, struct IKCPSEG *seg, int r) {
	IUINT16 fid = seg->fid;
	IUINT16 fty = seg->fty;
	struct fec_counts_t counts = FEC_COUNTS[fty];
	IUINT16 count = counts.original_count + counts.recovery_count - 1;
	while (1) {
		struct IQUEUEHEAD *p = seg->gid > 0 ? seg->node.prev : 0;

		if (seg->gid != count || seg->fty != fty) {
			return -2;
		}

		if (seg->recovery_data || (r && !ikcp_input_check_nack(seg->pid, kcp))) {
			iqueue_del(&seg->node, 1);
			if (!seg->recovery_data) {
#ifdef CHECK_PID
				if (!rp_arq_bitset_check(&kcp->pid_bs, seg->pid)) {
					nsDbgPrint("rp_arq_bitset_check failed for %d", (int)seg->pid);
					return -3;
				}
				// nsDbgPrint("rp_arq_bitset_clear for %d", (int)seg->pid);
				rp_arq_bitset_clear(&kcp->pid_bs, seg->pid);
#endif
				// memcpy(&saved_segs[seg->pid], &seg->pid, sizeof(struct SavedSeg));
				--kcp->n_snd;
			}
			ikcp_segment_free(kcp, seg);
		} else {
			iqueue_del(&seg->node, 2);
			if (seg->recovery_data) {
				ikcp_segment_free(kcp, seg);
			} else {
				seg->gid_end = false;
				if (r) {
					++seg->wrn;
					if (seg->wrn > RSND_COUNT) {
						seg->wrn = RSND_COUNT;
					}
				}
				struct IQUEUEHEAD *queue = arq_queue_get_from_wrn(kcp, seg->wrn);
				iqueue_add_tail(&seg->node, queue);
			}
		}

		if (!p) {
			break;
		}

		while (1) {
			if (p == &kcp->snd_wak) {
				return -1;
			}

			struct IKCPSEG *seg_prev = iqueue_entry(p, IKCPSEG, node);
			if (seg_prev->fid == fid) {
				seg = seg_prev;
				--count;
				break;
			}

			p = p->prev;
		}
	}
	return 0;
}

static int ikcp_input_handle_send_cur_nack(ikcpcb *kcp, struct IKCPSEG *seg, struct IQUEUEHEAD **next) {
	struct IKCPSEG *segs[FEC_COUNT_MAX] = { seg, 0 };
	int count = 1;
	IUINT16 fty = seg->fty;
	IUINT16 fid = seg->fid;
	while (1) {
		struct IQUEUEHEAD *p = seg->node.next;
		if (p == &kcp->snd_cur) {
			return 2;
		}

		seg = iqueue_entry(p, IKCPSEG, node);
		if (seg->fid == fid) {
			if (seg->fty != fty) {
				return -2;
			}

			if (seg->wrn == 0 || (!seg->recovery_data && ikcp_input_check_nack(seg->pid, kcp))) {
				return 1;
			}

			if (seg->gid != count) {
				return -1;
			}

			segs[count] = seg;
			++count;

			if (seg->gid_end) {
				break;
			}
		}
	}

	struct fec_counts_t counts = FEC_COUNTS[fty];
	if (count != counts.original_count + counts.recovery_count) {
		return -3;
	}

	struct IQUEUEHEAD *n = segs[0]->node.next;

	for (int i = 0; i < count; ++i) {
		seg = segs[i];
		struct IQUEUEHEAD *p = seg->node.next;
		if (p != &kcp->snd_cur) {
			struct IKCPSEG *next = iqueue_entry(p, IKCPSEG, node);
			next->wsn += seg->wsn + 1;
		}

		if (n == &seg->node) {
			n = n->next;
		}

		iqueue_del(&seg->node, 3);
		if (!seg->recovery_data) {
#ifdef CHECK_PID
			if (!rp_arq_bitset_check(&kcp->pid_bs, seg->pid)) {
				nsDbgPrint("rp_arq_bitset_check failed for %d fid %d fty %d gid %d wrn %d", (int)seg->pid, (int)seg->fid, (int)fty, (int)seg->gid, (int)seg->wrn);
				// struct SavedSeg *s = &saved_segs[seg->pid];
				// nsDbgPrint("saved pid %d fid %d fty %d gid %d wrn %d", (int)s->pid, (int)s->fid, (int)s->fty, (int)s->gid, (int)s->wrn);
				return -4;
			}
			// nsDbgPrint("rp_arq_bitset_clear for %d", (int)seg->pid);
			rp_arq_bitset_clear(&kcp->pid_bs, seg->pid);
#endif
			// memcpy(&saved_segs[seg->pid], &seg->pid, sizeof(struct SavedSeg));
			--kcp->n_snd;
		}
		ikcp_segment_free(kcp, seg);
	}

	*next = n;

	return 0;
}

static int ikcp_input_handle_nack(ikcpcb *kcp, struct IQUEUEHEAD *queue, int g, bool r)
{
	for (struct IQUEUEHEAD *p = queue->next, *next = p->next; p != queue; p = next, next = p->next) {
		struct IKCPSEG *seg = iqueue_entry(p, IKCPSEG, node);
		if (!ikcp_input_check_nack(seg->pid, kcp)) {
			iqueue_del(&seg->node, g);
#ifdef CHECK_PID
			if (!rp_arq_bitset_check(&kcp->pid_bs, seg->pid)) {
				nsDbgPrint("rp_arq_bitset_check failed for %d", (int)seg->pid);
				return -5;
			}
			// nsDbgPrint("rp_arq_bitset_clear for %d", (int)seg->pid);
			rp_arq_bitset_clear(&kcp->pid_bs, seg->pid);
#endif
			// memcpy(&saved_segs[seg->pid], &seg->pid, sizeof(struct SavedSeg));
			--kcp->n_snd;
			ikcp_segment_free(kcp, seg);
		}
	}
	return 0;
}

int ikcp_input(ikcpcb *kcp, char *data, int size)
{
	if (size < (int)sizeof(IUINT16))
		return -1;

	IUINT16 hdr = *(IUINT16 *)data;

	data += sizeof(IUINT16);
	size -= sizeof(IUINT16);

	IUINT16 fid = (hdr >> (GID_NBITS + CID_NBITS + 1)) & ((1 << FID_NBITS) - 1);
	IUINT16 gid = (hdr >> (CID_NBITS + 1)) & ((1 << GID_NBITS) - 1);
	IUINT16 cid = (hdr >> 1) & ((1 << CID_NBITS) - 1);
	IUINT16 reset = hdr & ((1 << 1) - 1);

	if (kcp->cid != cid) {
		return 0;
	}

	if (reset) {
		return -2;
	}

	if (size == 0) {
		if (fid == 0 && gid == ((IUINT16)-1 & ((1 << GID_NBITS) - 1))) {
			if (!kcp->session_established) {
				kcp->session_established = true;
				nsDbgPrint("kcp session_established");
			}
			return 0;
		} else {
			return -5;
		}
	}

#ifdef TIMING_INFO
	if (saved_fid != (IUINT16)-1 && ((fid - saved_fid) & ((1 << FID_NBITS) - 1)) < (1 << (FID_NBITS - 1))) {
		u64 timing = svcGetSystemTick() - saved_timing;
		saved_fid = (IUINT16)-1;
		saved_timing = 0;

		if (timing > max_timing) {
			max_timing = timing;
		}
		u64 next_timing = svcGetSystemTick();
		if ((next_timing - last_timing) / SYSCLOCK_ARM11 >= 1) {
			last_timing = next_timing;
			u64 mono_ns = max_timing * 1000000000 / SYSCLOCK_ARM11;
			nsDbgPrint("kcp delay %u.%u", (unsigned)(mono_ns / 1000000 % 60000), (unsigned)(mono_ns % 1000000));
			max_timing = 0;
		}
	}
#endif

	kcp->n_nacks = 0;

	IUINT16 *ptr = (IUINT16 *)data;
	size /= 2;
	while (size) {
		IUINT16 val = *ptr;
		IUINT16 nack_start = (val >> count_nbits) & ((1 << PID_NBITS) - 1);
		IUINT16 nack_count_0 = size == 1 ? (1 << (PID_NBITS - 2)) - 1 : val & ((1 << count_nbits) - 1);

		if (kcp->n_nacks >= sizeof(kcp->nacks) / sizeof(*kcp->nacks)) {
			return -4;
		}

		kcp->nacks[kcp->n_nacks][0] = nack_start;
		kcp->nacks[kcp->n_nacks][1] = nack_count_0;
		++kcp->n_nacks;

		++ptr;
		--size;
	}

	for (struct IQUEUEHEAD *p = kcp->snd_wak.next, *next = p->next; p != &kcp->snd_wak; p = next, next = p->next) {
		struct IKCPSEG *seg = iqueue_entry(p, IKCPSEG, node);
		bool should_break = seg->fid == fid && seg->gid == gid;
		if (seg->gid_end) {
			int ret;
			if ((ret = ikcp_input_handle_send_wak_nack(kcp, seg, true)) < 0) {
				return ret * 0x10 - 3;
			}
		}
		if (should_break) {
			break;
		}
	}

	for (int i = 0; i < RSND_COUNT; ++i) {
		struct IQUEUEHEAD *queue = &kcp->rsnd_lsts[i];
		int ret = ikcp_input_handle_nack(kcp, queue, 4 + i, false);
		if (ret < 0) {
			return ret * 0x10 - 5 - i;
		}
	}

	if (1) for (struct IQUEUEHEAD *p_0 = kcp->snd_cur.next, *p = p_0, *next = p->next; p != &kcp->snd_cur; p = next, next = p->next) {
		struct IKCPSEG *seg = iqueue_entry(p, IKCPSEG, node);
		if (seg->wrn && seg->gid == 0 && !ikcp_input_check_nack(seg->pid, kcp)) {
			int ret = ikcp_input_handle_send_cur_nack(kcp, seg, &next);
			if (ret < 0) {
				__atomic_store_n(&kcp->rp_output_retry, true, __ATOMIC_RELEASE);
				return ret * 0x10 - 4;
			} else if (ret == 0 && p == p_0) {
				__atomic_store_n(&kcp->rp_output_retry, true, __ATOMIC_RELEASE);
			}
		}
	}

	kcp->session_new_data_received = true;
	return 0;
}


//---------------------------------------------------------------------
// ikcp_encode_seg
//---------------------------------------------------------------------

// FIXME:
// Currently works on little endian only

// Outer header for fec
static void ikcp_encode_fec_hdr(struct IKCPSEG *seg) {
	IUINT16 *p = (IUINT16 *)ikcp_get_packet_data_buf(seg->data_buf);
	*p = (seg->fty & ((1 << FTY_NBITS) - 1)) | ((seg->gid & ((1 << GID_NBITS) - 1)) << FTY_NBITS) | ((seg->fid & ((1 << FID_NBITS) - 1)) << (FTY_NBITS + GID_NBITS));
}

// Inner header for arq
static void ikcp_encode_arq_hdr(ikcpcb *kcp, struct IKCPSEG *seg) {
	IUINT16 *p = (IUINT16 *)ikcp_get_fec_data_buf(seg->data_buf);
	// nsDbgPrint("%x", (unsigned)*p);
	*p |= (seg->pid & ((1 << PID_NBITS) - 1)) | ((kcp->cid & ((1 << CID_NBITS) - 1)) << PID_NBITS);
}


//---------------------------------------------------------------------
// ikcp_flush
//---------------------------------------------------------------------
static int ikcp_send_cur_get_delay(ikcpcb *kcp)
{
	if (iqueue_is_empty(&kcp->snd_cur))
		return -1;

	IKCPSEG *seg;
	seg = iqueue_entry(kcp->snd_cur.next, IKCPSEG, node);
	return seg->wsn;
}

static const enum FEC_TYPE FEC_TYPES[] = {
	FEC_TYPE_1_3, // rsnd_lst[RSND_COUNT - 1]
	FEC_TYPE_1_2, // ...
	FEC_TYPE_2_3, // rsnd_lst[0]
	FEC_TYPE_1_1, // snd_lst
};

_Static_assert(sizeof(FEC_TYPES) / sizeof(*FEC_TYPES) == ARQ_QUEUE_COUNT);

static const enum FEC_TYPE FEC_FALLBACK_TYPE = FEC_TYPE_1_2; // must have (original_count == 1)

static enum FEC_TYPE fec_type_from_queue(enum ARQ_QUEUE queue) {
	return FEC_TYPES[queue];
}

struct arq_seg_iter_t {
	enum ARQ_QUEUE queue;
	struct IKCPSEG *seg;
};

static struct arq_seg_iter_t arq_seg_iter_init(ikcpcb *kcp) {
	struct arq_seg_iter_t iter = {0, 0};

	while (true) {
		struct IQUEUEHEAD *queue = arq_queue_get(kcp, iter.queue);
		if (!queue) {
			return iter;
		}

		struct IQUEUEHEAD *entry = queue->next;
		if (entry == queue) {
			++iter.queue;
			continue;
		}

		iter.seg = iqueue_entry(entry, IKCPSEG, node);
		break;
	}

	return iter;
}

static struct arq_seg_iter_t arq_seg_iter_next(ikcpcb *kcp, struct arq_seg_iter_t iter) {
	if (!iter.seg) {
		return iter;
	}

	while (true) {
		struct IQUEUEHEAD *queue = arq_queue_get(kcp, iter.queue);
		if (!queue) {
			return iter;
		}

		struct IQUEUEHEAD *entry = iter.seg->node.next;
		if (entry == queue) {
			iter.seg = 0;
			do {
				++iter.queue;
				queue = arq_queue_get(kcp, iter.queue);
				if (!queue) {
					return iter;
				}
				entry = queue->next;
			} while (entry == queue);
		}

		iter.seg = iqueue_entry(entry, IKCPSEG, node);
		break;
	}
	return iter;
}

static int ikcp_insert_standalone_send_cur(ikcpcb *kcp, struct IKCPSEG *seg)
{
	struct IQUEUEHEAD *p;
	p = kcp->snd_cur.next;
	while (1) {
		if (p == &kcp->snd_cur) {
			iqueue_ins_before(&seg->node, p);
			return 0;
		}
		struct IKCPSEG *cur = iqueue_entry(p, IKCPSEG, node);
		if (cur->wsn < seg->wsn) {
			seg->wsn -= cur->wsn;
			--seg->wsn;
			p = p->next;
			continue;
		} else if (cur->wsn == seg->wsn) {
			seg->wsn = 0;
			int ret = 0;
			while (1) {
				p = p->next;
				++ret;
				if (p == &kcp->snd_cur) {
					iqueue_ins_before(&seg->node, p);
					return ret;
				}
				cur = iqueue_entry(p, IKCPSEG, node);
				if (cur->wsn) {
					--cur->wsn;
					iqueue_ins_before(&seg->node, p);
					return ret;
				}
			}
			return -2;
		} else { // cur->wsn > seg->wsn
			cur->wsn -= seg->wsn;
			--cur->wsn;
			iqueue_ins_before(&seg->node, p);
			return 0;
		}
	}
	return -1;
}

static int ikcp_insert_send_cur(ikcpcb *kcp, struct arq_seg_iter_t *iter)
{
	iqueue_del(&iter->seg->node, 5);
	return ikcp_insert_standalone_send_cur(kcp, iter->seg);
}

static int ikcp_reset_send_wak(ikcpcb *kcp)
{
	IKCPSEG *seg;
	for (struct IQUEUEHEAD *p = kcp->snd_wak.next, *next = p->next; p != &kcp->snd_wak; p = next, next = p->next) {
		seg = iqueue_entry(p, IKCPSEG, node);
		if (seg->gid_end) {
			int ret;
			if ((ret = ikcp_input_handle_send_wak_nack(kcp, seg, false)) != 0) {
				return ret * 0x10 - 3;
			}
		}
	}
	return 0;
}

static int ikcp_queue_send_cur(ikcpcb *kcp)
{
	struct arq_seg_iter_t iters[FEC_COUNT_MAX] = { arq_seg_iter_init(kcp), { 0 } };
	if (!iters[0].seg) {
#ifdef CHECK_PID
		if (rp_arq_bitset_check_n_wrapped(&kcp->pid_bs, (kcp->pid + pid_bs_offset) & ((1 << PID_NBITS) - 1), pid_bs_n)) {
			int ret;
			if ((ret = ikcp_reset_send_wak(kcp)) < 0) {
				return ret * 0x100 - 1;
			}
			iters[0] = arq_seg_iter_init(kcp);
		}
#endif
		if (!iters[0].seg)
			return 1;
	}

	// nsDbgPrint("send cur pid %d", iters[0].seg->pid);
	enum FEC_TYPE fec_type = fec_type_from_queue(iters[0].queue);
	struct fec_counts_t counts = FEC_COUNTS[fec_type];

	if (counts.original_count != 1) {
		int use_fallback = 0;
		for (int i = 1; i < counts.original_count; ++i) {
			iters[i] = arq_seg_iter_next(kcp, iters[i - 1]);
			if (!iters[i].seg) {
				use_fallback = 1;
				break;
			}
		}

		if (use_fallback) {
			fec_type = FEC_FALLBACK_TYPE;
			counts = FEC_COUNTS[fec_type];
			if (counts.original_count != 1) {
				return -2;
			}
		} else  {
			iters[0].seg->fid = kcp->fid;
			++kcp->fid;
			kcp->fid &= ((1 << FID_NBITS) - 1);
			iters[0].seg->fty = fec_type;
			iters[0].seg->gid = 0;
			iters[0].seg->wsn = 0;
			ikcp_encode_arq_hdr(kcp, iters[0].seg);
			int wsn = 0;
			int ret;
			ret = ikcp_insert_send_cur(kcp, &iters[0]);
			if (ret < 0) {
				return -12;
			}
			wsn += ret;

			int count = 1;
			--counts.original_count;

			while (1) {
				if (!counts.original_count)
					break;

				iters[count].seg->fid = iters[0].seg->fid;
				iters[count].seg->fty = fec_type;
				iters[count].seg->gid = count;
				iters[count].seg->wsn = count * fec_send_intervals[fec_type] + wsn;

				ikcp_encode_arq_hdr(kcp, iters[count].seg);
				ret = ikcp_insert_send_cur(kcp, &iters[count]);
				if (ret < 0) {
					return -14;
				}
				wsn += ret;

				++count;
				--counts.original_count;
			}

			FecalEncoder fecal_encoder = rp_kcp_fecal_encoder;
			void *data_ptrs[count];
			for (int i = 0; i < count; ++i) {
				data_ptrs[i] = ikcp_get_fec_data_buf(iters[i].seg->data_buf);
			}
			if (fecal_encoder_init(fecal_encoder, count, data_ptrs, count * FEC_DATA_SIZE) != 0) {
				return -15;
			}

			int recovery_index = 0;
			while (1) {
				if (!counts.recovery_count)
					break;

				struct IKCPSEG *seg = ikcp_segment_malloc(kcp);
				if (!seg) {
					return -17;
				}
				*seg = (struct IKCPSEG){ 0 };
				seg->pid = (IUINT16)-1;
				seg->fid = iters[0].seg->fid;
				seg->fty = fec_type;
				seg->gid = count;
				seg->wsn = count * fec_send_intervals[fec_type] + wsn;
				seg->recovery_data = true;
				if (counts.recovery_count == 1) {
					seg->gid_end = true;
				}
				seg->data_buf = ikcp_seg_data_buf_malloc();
				if (!seg->data_buf) {
					return -16;
				}
				seg->is_kcp_seg_data = true;
				void *data_ptr = ikcp_get_fec_data_buf(seg->data_buf);
				FecalSymbol fecal_symbol = {
					.Data = data_ptr,
					.Bytes = FEC_DATA_SIZE,
					.Index = recovery_index,
				};
				if (fecal_encode(fecal_encoder, &fecal_symbol) != 0) {
					return -18;
				}
				ret = ikcp_insert_standalone_send_cur(kcp, seg);
				if (ret < 0) {
					return -19;
				}
				wsn += ret;

				++count;
				++recovery_index;
				--counts.recovery_count;
			}

			if (!counts.original_count && !counts.recovery_count) {
				return 0;
			}

			return -5;
		}
	}

	iters[0].seg->fid = kcp->fid;
	++kcp->fid;
	kcp->fid &= ((1 << FID_NBITS) - 1);
	iters[0].seg->fty = fec_type;
	iters[0].seg->gid = 0;
	iters[0].seg->wsn = 0;
	if (!counts.recovery_count) {
		iters[0].seg->gid_end = true;
	} else {
		iters[0].seg->recovery_data = true;
		iters[0].seg->weak_data = true;
	}
	ikcp_encode_arq_hdr(kcp, iters[0].seg);
	int wsn = 0;
	int ret;
	ret = ikcp_insert_send_cur(kcp, &iters[0]);
	if (ret < 0) {
		return -6;
	}
	wsn += ret;

	int count = 1;
	counts.original_count = 0;
	while (1) {
		if (!counts.recovery_count)
			break;

		struct IKCPSEG *seg = ikcp_segment_malloc(kcp);
		if (!seg) {
			return -7;
		}
		*seg = *iters[0].seg;
		seg->gid = count;
		seg->wsn = count * fec_send_intervals[fec_type] + wsn;
		if (counts.recovery_count == 1) {
			seg->gid_end = true;
			seg->recovery_data = false;
			seg->weak_data = false;
		}
		ret = ikcp_insert_standalone_send_cur(kcp, seg);
		if (ret < 0) {
			return -8;
		}
		wsn += ret;

		++count;
		--counts.recovery_count;
	}
	if (!counts.recovery_count)
		return 0;

	return -4;
}

static int ikcp_send_cur(ikcpcb *kcp)
{
	IKCPSEG *seg;
	seg = iqueue_entry(kcp->snd_cur.next, IKCPSEG, node);
	ikcp_encode_fec_hdr(seg);

	const int len = PACKET_SIZE;
	int ret = ikcp_output(kcp, ikcp_get_packet_data_buf(seg->data_buf), len);
	if (ret == 0) {
		return 1;
	}
	if (ret != len) {
		return -5;
	}

	iqueue_del(&seg->node, 7);
	if (seg->recovery_data) {
		if (!seg->weak_data && seg->is_kcp_seg_data) {
			ikcp_seg_data_buf_free(seg->data_buf);
			seg->weak_data = true;
			// seg->data_buf = NULL;
		}
	} else {
#ifdef TIMING_INFO
		if (
			saved_fid == (IUINT16)-1
			|| ((seg->fid - saved_fid) & ((1 << FID_NBITS) - 1)) >= (1 << (FID_NBITS - 1))
		) {
			saved_timing = svcGetSystemTick();
			saved_fid = seg->fid;
		}
#endif
	}
	if (seg->term_notify) {
		rp_term_notify();
		seg->term_notify = false;
	}
	iqueue_add_tail(&seg->node, &kcp->snd_wak);
	return 0;
}

int ikcp_send_next(ikcpcb *kcp)
{
	int ret;

	if (!kcp->session_established) {
		struct IKCPSEG *seg = ikcp_segment_malloc(kcp);
		if (!seg) {
			return -3;
		}
		*seg = (struct IKCPSEG){};

		seg->data_buf = ikcp_seg_data_buf_malloc();
		if (!seg->data_buf) {
			seg->weak_data = true;
			ikcp_segment_free(kcp, seg);
			return -2;
		}
		seg->is_kcp_seg_data = true;

		seg->gid = (IUINT16)-1 & ((1 << GID_NBITS) - 1);
		seg->fid = kcp->cid;
		IUINT16 hdr = 0;
		IUINT16 *p = (IUINT16 *)ikcp_get_fec_data_buf(seg->data_buf);
		*p = hdr;
		ikcp_encode_arq_hdr(kcp, seg);
		ikcp_encode_fec_hdr(seg);

		const int len = sizeof(IUINT16);
		while (1) {
			__atomic_store_n(&kcp->rp_output_retry, false, __ATOMIC_RELEASE);
			int ret = ikcp_output(kcp, ikcp_get_packet_data_buf(seg->data_buf), len);
			if (ret == 0) {
				continue;
			}
			if (ret != len) {
				return -5;
			}
			break;
		}

		ikcp_segment_free(kcp, seg);
		return 0;
	}

	while (1) {
		if (ikcp_send_cur_get_delay(kcp) < 0) {
			return 1;
		}
		__atomic_store_n(&kcp->rp_output_retry, false, __ATOMIC_RELEASE);
		ret = ikcp_send_cur(kcp);
		if (ret < 0) {
			return ret * 0x10 - 1;
		}
		if (ret == 0) {
			return 0;
		}
	};

	return 0;
}


int ikcp_wndsize(ikcpcb *kcp, int sndwnd, int curwnd)
{
	if (kcp) {
		if (sndwnd > 0) {
			kcp->n_snd_max = _imin_(sndwnd, ARQ_BUFS_COUNT);
		} else {
			return -1;
		}
		if (curwnd > 0) {
			kcp->n_cur_max = _imin_(curwnd, ARQ_CUR_BUFS_COUNT);

			for (int i = 0; i < FEC_TYPE_COUNT; ++i) {
				struct fec_counts_t counts = FEC_COUNTS[i];
				// Minimum factor for send interval
				int count = _imax_(counts.original_count + counts.recovery_count, 3);
				fec_send_intervals[i] = (kcp->n_cur_max + count / 2) / count;
			}
		} else {
			return -2;
		}
	}
	return 0;
}


int ikcp_queue_get_free(ikcpcb *kcp)
{
	return kcp->n_snd_max - kcp->n_snd;
}

int ikcp_send_ready_and_get_delay(ikcpcb *kcp)
{
	if (kcp->n_snd <= 0) {
		return -3;
	}

	if (!kcp->session_established) {
		return 0;
	}

	int ret;
	if (ikcp_send_cur_get_delay(kcp) != 0) {
		if ((ret = ikcp_queue_send_cur(kcp)) < 0) {
			return ret * 0x10 - 2;
		}
	}

	kcp->session_new_data_received = false;
	return ikcp_send_cur_get_delay(kcp);
}
