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
#include "fecal.h"
#include "constants.h"

#include <stddef.h>
#include <string.h>

#pragma GCC diagnostic ignored "-Wunused-parameter"
#pragma GCC diagnostic ignored "-Wunused-function"
#pragma GCC diagnostic ignored "-Wunused-variable"

enum FEC_TYPE {
	FEC_TYPE_1_1,
	FEC_TYPE_4_5,
	FEC_TYPE_3_4,
	FEC_TYPE_2_3,
	FEC_TYPE_3_5,
	FEC_TYPE_2_4,
	FEC_TYPE_2_5,
	FEC_TYPE_1_3,
	FEC_TYPE_MAX = FEC_TYPE_1_3,
	// coded as FEC_TYPE_1_1 with top bit in group id set to differentiate
	FEC_TYPE_1_2,
	FEC_TYPE_COUNT,
};

#define FEC_TYPE_BITS_COUNT (3)

_Static_assert(FEC_TYPE_MAX < (1 << FEC_TYPE_BITS_COUNT));

struct fec_counts_t {
	IUINT8 original_count, recovery_count;
};

static const struct fec_counts_t FEC_COUNTS[] = {
	{1, 0},
	{4, 1},
	{3, 1},
	{2, 1},
	{3, 2},
	{2, 2},
	{2, 3},
	{1, 2},
	{1, 1},
};

_Static_assert(sizeof(FEC_COUNTS) / sizeof(*FEC_COUNTS) == FEC_TYPE_COUNT);

static int fec_send_intervals[FEC_TYPE_COUNT];

// max of original_count + recovery_count
#define FEC_COUNT_MAX (5)

//=====================================================================
// KCP BASIC
//=====================================================================


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
static IKCPSEG* ikcp_segment_new(ikcpcb *kcp)
{
	return (IKCPSEG*)mp_malloc(&kcp->seg_pool);
}

// delete a segment
static void ikcp_segment_delete(ikcpcb *kcp, IKCPSEG *seg)
{
	if (!seg->skip_free_data_buf)
		free_seg_data_buf(seg->data_buf);
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
	kcp->cid = cid & ((1 << 2) - 1);

	iqueue_init(&kcp->snd_lst);
	for (int i = 0; i < RSND_COUNT; ++i) {
		iqueue_init(&kcp->rsnd_lsts[i]);
	}
	iqueue_init(&kcp->snd_cur);
	iqueue_init(&kcp->snd_wak);
	kcp->n_snd_max = ARQ_BUFS_COUNT;
	kcp->n_cur_max = ARQ_CUR_BUFS_COUNT;
	kcp->rp_output_retry = false;

	int ret;

	ret = mp_init(
		ARQ_SEG_SIZE,
		ARQ_SEG_MEM_COUNT,
		kcp->seg_mem,
		&kcp->seg_pool);
	if (ret != 0)
		return -1;

	return 0;
}


//---------------------------------------------------------------------
// user/upper level send
//---------------------------------------------------------------------
int ikcp_queue(ikcpcb *kcp, char *buffer, int len)
{
	IKCPSEG *seg;

	if (len <= 0) return -1;

	// TODO
	// only support (len == ARQ_DATA_SIZE) actually
	if (len > ARQ_DATA_SIZE) {
		return -2;
	}

	if (ikcp_queue_get_free(kcp) <= 0 ) {
		return -4;
	}

	seg = ikcp_segment_new(kcp);
	if (seg == NULL) {
		return -3;
	}

	*seg = (struct IKCPSEG){ 0 };
	seg->data_buf = buffer;
	seg->pid = kcp->pid;
	++kcp->pid;

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

static bool ikcp_input_check_nack(IUINT16 pid, char *data, int size) {
	// TODO maybe optimize
	IUINT16 *ptr = (IUINT16 *)data;
	size /= 2;

	for (int i = 0; i < size; ++i) {
		IUINT16 val = ptr[i];
		IUINT16 nack_start = (val >> 6) & ((1 << 10) - 1);
		IUINT16 nack_count_0 = val & ((1 << 6) - 1);
		IUINT16 pid_diff = (pid - nack_start) & ((1 << 10) - 1);
		if (pid_diff <= nack_count_0) {
			return true;
		}
	}

	return false;
}

static int ikcp_input_handle_nack(ikcpcb *kcp, struct IKCPSEG *seg, char *data, int size) {
	while (1) {
		if (seg->pid == (IUINT16)-1 || !ikcp_input_check_nack(seg->pid, data, size)) {
			iqueue_del(&seg->node);
			ikcp_segment_delete(kcp, seg);
		} else {
			iqueue_del(&seg->node);
			if (seg->free_instead_of_resend) {
				ikcp_segment_delete(kcp, seg);
			} else {
				++seg->wrn;
				if (seg->wrn > RSND_COUNT) {
					seg->wrn = RSND_COUNT;
				}
				struct IQUEUEHEAD *queue = arq_queue_get_from_wrn(kcp, seg->wrn);
				iqueue_add_tail(&seg->node, queue);
			}
		}

		if (seg->gid == 0)
			break;

		struct IQUEUEHEAD *p = seg->node.prev;
		while (1) {
			if (p == &seg->node) {
				return -1;
			}

			struct IKCPSEG *seg_prev = iqueue_entry(p, IKCPSEG, node);
			if (seg_prev->fid == seg->fid) {
				seg = seg_prev;
				break;
			}

			p = p->prev;
		}
	}
	return 0;
}

static int ikcp_input_handle_send_cur_nack(ikcpcb *kcp, struct IKCPSEG *seg, char *data, int size) {
	struct IKCPSEG *segs[FEC_COUNT_MAX] = { seg, 0 };
	int count = 1;
	while (!seg->gid_end) {
		seg = iqueue_entry(seg->node.next, IKCPSEG, node);
		if (seg->fid == segs[0]->fid) {
			if (seg->pid != (IUINT16)-1) {
				if (seg->wrn == 0 || ikcp_input_check_nack(seg->pid, data, size)) {
					return 1;
				}
			}

			if (seg->gid != count) {
				return -1;
			}

			segs[count] = seg;
			++count;
		}
	}
	for (int i = 0; i > count; ++i) {
		iqueue_del(&segs[i]->node);
		ikcp_segment_delete(kcp, segs[i]);
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

	IUINT16 fid = (hdr >> 6) & ((1 << 10) - 1);
	IUINT16 gid = (hdr >> 3) & ((1 << 3) - 1);
	IUINT16 cid = (hdr >> 1) & ((1 << 2) - 1);
	IUINT16 reset = hdr & ((1 << 1) - 1);

	if (kcp->cid == cid && reset) {
		return -2;
	}

	if (kcp->cid != cid) {
		return 0;
	}

	for (struct IQUEUEHEAD *p = kcp->snd_wak.next, *next = p->next; p != &kcp->snd_wak; p = next, next = p->next) {
		struct IKCPSEG *seg = iqueue_entry(p, IKCPSEG, node);
		if (seg->fid == fid && seg->gid == gid)
			break;
		if (seg->gid_end) {
			if (ikcp_input_handle_nack(kcp, seg, data, size) != 0) {
				return -3;
			}
		}
	}

	for (int i = 0; i < RSND_COUNT; ++i) {
		struct IQUEUEHEAD *queue = &kcp->rsnd_lsts[i];
		for (struct IQUEUEHEAD *p = queue->next, *next = p->next; p != queue; p = next, next = p->next) {
			struct IKCPSEG *seg = iqueue_entry(p, IKCPSEG, node);
			if (!ikcp_input_check_nack(seg->pid, data, size)) {
				iqueue_del(&seg->node);
				ikcp_segment_delete(kcp, seg);
			}
		}
	}

	for (struct IQUEUEHEAD *p = kcp->snd_cur.next, *next = p->next; p != &kcp->snd_cur; p = next, next = p->next) {
		struct IKCPSEG *seg = iqueue_entry(p, IKCPSEG, node);
		if (seg->wrn && seg->gid == 0 && !ikcp_input_check_nack(seg->pid, data, size)) {
			int ret = ikcp_input_handle_send_cur_nack(kcp, seg, data, size);
			if (ret < 0) {
				return -4;
			} else if (ret == 0 && p == kcp->snd_cur.next) {
				kcp->rp_output_retry = true;
			}
		}
	}

	kcp->session_established = true;
	kcp->session_new_data_received = true;
	return 0;
}


//---------------------------------------------------------------------
// ikcp_encode_seg
//---------------------------------------------------------------------

static char *ikcp_get_fec_data_buf(struct IKCPSEG *seg) {
	return seg->data_buf - (ARQ_OVERHEAD_SIZE - FEC_OVERHEAD_SIZE);
}

static char *ikcp_get_packet_data_buf(struct IKCPSEG *seg) {
	return seg->data_buf - ARQ_OVERHEAD_SIZE;
}

// FIXME:
// Currently works on little endian only

// Outer header for fec
static void ikcp_encode_fec_hdr(struct IKCPSEG *seg) {
	IUINT16 *p = (IUINT16 *)ikcp_get_packet_data_buf(seg);
	if (seg->fty == FEC_TYPE_1_2) {
		*p = (((seg->gid & ((1 << 2) - 1)) | (1 << 2)) << 3);
	} else {
		*p = (seg->fty & ((1 << 3) - 1)) | ((seg->gid & ((1 << 3) - 1)) << 3);
	}
	*p |=  ((seg->fid & ((1 << 10) - 1)) << 6);
}

// Inner header for arq
static void ikcp_encode_arq_hdr(ikcpcb *kcp, struct IKCPSEG *seg) {
	IUINT16 *p = (IUINT16 *)ikcp_get_fec_data_buf(seg);
	*p |= (seg->pid & ((1 << 10) - 1)) | ((kcp->cid & ((1 << 2) - 1)) << 10);
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
	FEC_TYPE_2_5, // rsnd_lst[0]
	FEC_TYPE_1_1, // snd_lst
};

_Static_assert(sizeof(FEC_TYPES) / sizeof(*FEC_TYPES) == ARQ_QUEUE_COUNT);

static const enum FEC_TYPE FEC_FALLBACK_TYPE = FEC_TYPE_1_2; // must have (original_count == 1) for now

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

static int ikcp_insert_send_cur(ikcpcb *kcp, struct IKCPSEG *seg)
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

static int ikcp_insert_send_cur_from_iter(ikcpcb *kcp, struct arq_seg_iter_t *iter)
{
	iqueue_del(&iter->seg->node);
	return ikcp_insert_send_cur(kcp, iter->seg);
}

static void ikcp_reset_send_wak(ikcpcb *kcp)
{
	IKCPSEG *seg;
	for (struct IQUEUEHEAD *p = kcp->snd_wak.next, *next = p->next; p != &kcp->snd_wak; p = next, next = p->next) {
		seg = iqueue_entry(p, IKCPSEG, node);
		if (seg->wrn)
			continue;
		struct IQUEUEHEAD *queue = arq_queue_get_from_wrn(kcp, seg->wrn);
		iqueue_del(&seg->node);
		iqueue_add_tail(&seg->node, queue);
	}
}

static int ikcp_queue_send_cur(ikcpcb *kcp)
{
	struct arq_seg_iter_t iters[FEC_COUNT_MAX] = { arq_seg_iter_init(kcp), { 0 } };
	if (!iters[0].seg) {
		if (!kcp->session_established) {
			ikcp_reset_send_wak(kcp);
			iters[0] = arq_seg_iter_init(kcp);
		}
		if (!iters[0].seg)
			return 1;
	}

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
			iters[0].seg->fty = fec_type;
			iters[0].seg->gid = 0;
			iters[0].seg->wsn = 0;
			ikcp_encode_arq_hdr(kcp, iters[0].seg);
			int wsn = 0;
			int ret;
			ret = ikcp_insert_send_cur_from_iter(kcp, &iters[0]);
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
				ret = ikcp_insert_send_cur_from_iter(kcp, &iters[count]);
				if (ret < 0) {
					return -14;
				}
				wsn += ret;

				++count;
				--counts.original_count;
			}

			const int fecal_size = fecal_encoder_size();
			char fecal_encoder_ws[fecal_size] ALIGNED(sizeof(void *));
			FecalEncoder fecal_encoder = (FecalEncoder)fecal_encoder_ws;
			void *data_ptrs[count];
			for (int i = 0; i < count; ++i) {
				data_ptrs[i] = ikcp_get_fec_data_buf(iters[i].seg);
			}
			if (fecal_encoder_init(fecal_encoder, count, data_ptrs, count * FEC_DATA_SIZE) != 0) {
				return -15;
			}

			int recovery_index = 0;
			while (1) {
				if (!counts.recovery_count)
					break;

				struct IKCPSEG *seg = ikcp_segment_new(kcp);
				if (!seg) {
					return -17;
				}
				*seg = (struct IKCPSEG){ 0 };
				seg->pid = (IUINT16)-1;
				seg->fid = iters[0].seg->fid;
				seg->fty = fec_type;
				seg->gid = count;
				seg->wsn = count * fec_send_intervals[fec_type] + wsn;
				seg->free_instead_of_resend = true;
				if (counts.recovery_count == 1) {
					iters[count].seg->gid_end = true;
				}
				seg->data_buf = alloc_seg_buf();
				if (!seg->data_buf) {
					return -16;
				}
				void *data_ptr = ikcp_get_fec_data_buf(seg);
				FecalSymbol fecal_symbol = {
					.Data = data_ptr,
					.Bytes = FEC_DATA_SIZE,
					.Index = recovery_index,
				};
				if (fecal_encode(fecal_encoder, &fecal_symbol) != 0) {
					return -18;
				}
				ret = ikcp_insert_send_cur(kcp, seg);
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
	iters[0].seg->fty = fec_type;
	iters[0].seg->gid = 0;
	iters[0].seg->wsn = 0;
	if (!counts.recovery_count) {
		iters[0].seg->gid_end = true;
	}
	ikcp_encode_arq_hdr(kcp, iters[0].seg);
	int wsn = 0;
	int ret;
	ret = ikcp_insert_send_cur_from_iter(kcp, &iters[0]);
	if (ret < 0) {
		return -6;
	}
	wsn += ret;

	int count = 1;
	counts.original_count = 0;
	while (1) {
		if (!counts.recovery_count)
			break;

		struct IKCPSEG *seg = ikcp_segment_new(kcp);
		if (!seg) {
			return -7;
		}
		*seg = *iters[0].seg;
		seg->gid = count;
		seg->wsn = count * fec_send_intervals[fec_type] + wsn;
		iters[0].seg->free_instead_of_resend = true;
		iters[0].seg->skip_free_data_buf = true;
		if (counts.recovery_count == 1) {
			seg->gid_end = true;
		}
		ret = ikcp_insert_send_cur(kcp, seg);
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
	if (ikcp_send_cur_get_delay(kcp) < 0) {
		return -2;
	}

	IKCPSEG *seg;
	seg = iqueue_entry(kcp->snd_cur.next, IKCPSEG, node);
	ikcp_encode_fec_hdr(seg);

	const int len = PACKET_SIZE;
	int ret = ikcp_output(kcp, ikcp_get_packet_data_buf(seg), len);
	if (ret == 0) {
		return 1;
	}
	if (ret != len) {
		return -5;
	}

	iqueue_del(&seg->node);
	if (seg->free_instead_of_resend) {
		if (!seg->skip_free_data_buf) {
			free_seg_data_buf(seg->data_buf);
			seg->skip_free_data_buf = true;
			seg->data_buf = NULL;
		}
	}
	iqueue_add_tail(&seg->node, &kcp->snd_wak);
	return 0;
}

int ikcp_send_next(ikcpcb *kcp)
{
	int ret;
	do {
		kcp->rp_output_retry = false;
	} while ((ret = ikcp_send_cur(kcp)) > 0);
	if (ret < 0) {
		return ret * 0x10 - 1;
	}

	return 0;
}


int ikcp_wndsize(ikcpcb *kcp, int sndwnd, int curwnd)
{
	if (kcp) {
		if (sndwnd > 0) {
			kcp->n_snd_max = _imin_(sndwnd, ARQ_BUFS_COUNT);
		}
		if (curwnd > 0) {
			kcp->n_cur_max = _imin_(curwnd, ARQ_CUR_BUFS_COUNT);

			for (int i = 0; i < FEC_TYPE_COUNT; ++i) {
				struct fec_counts_t counts = FEC_COUNTS[i];
				// Minimum factor for send interval
				int count = _imax_(counts.original_count + counts.recovery_count, 3);
				fec_send_intervals[i] = (kcp->n_cur_max + count / 2) / count;
			}
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
		return -1;
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
