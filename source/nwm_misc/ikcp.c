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
// #include "fecal.h"
#include "constants.h"

#include <stddef.h>
#include <string.h>

#pragma GCC diagnostic ignored "-Wunused-parameter"
#pragma GCC diagnostic ignored "-Wunused-function"
#pragma GCC diagnostic ignored "-Wunused-variable"

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

static inline long _itimediff(IUINT32 later, IUINT32 earlier)
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
	free_seg_data_buf(seg->data_buf);
	mp_free(&kcp->seg_pool, seg);
}

// output segment
static int ikcp_output(ikcpcb *kcp, void *data, int size)
{
	if (size == 0) return 0;
	return rp_udp_output(data, size, kcp);
}


//---------------------------------------------------------------------
// create a new kcpcb
//---------------------------------------------------------------------
int ikcp_create(ikcpcb* kcp, IUINT16 cid)
{
	kcp->cid = cid;
	kcp->pid = 0;
	kcp->qid = 0;

	ISNDLST_INIT(kcp->snd_lst);
	for (int i = 0; i < RSND_COUNT_MAX; ++i) {
		ISNDLST_INIT(kcp->rsnd_lsts[i]);
	}
	iqueue_init(&kcp->snd_cur);
	kcp->n_snd = 0;
	kcp->n_snd_max = SEND_BUFS_COUNT;
	kcp->rp_output_retry = false;

	int ret;

	ret = mp_init(
		ARQ_SEG_SIZE,
		SEND_BUFS_COUNT,
		kcp->seg_mem,
		&kcp->seg_pool);
	if (ret != 0)
		return -1;

	return 0;
}


//---------------------------------------------------------------------
// user/upper level send, returns at/below zero for error
//---------------------------------------------------------------------
int ikcp_queue(ikcpcb *kcp, char *buffer, int len)
{
	IKCPSEG *seg;

	if (len <= 0) return -1;

	if (len > ARQ_DATA_SIZE) {
		return -2;
	}

	if (!ikcp_can_queue(kcp)) {
		return -4;
	}

	seg = ikcp_segment_new(kcp);
	if (seg == NULL) {
		return -3;
	}

	seg->data_buf = buffer;
	seg->pid = kcp->pid;
	seg->qid = 0;
	seg->fid = 0;
	seg->fty = 0;
	seg->gid = 0;
	seg->wsn = 0;
	seg->wrn = 0;
	++kcp->pid;

	iqueue_init(&seg->node);
	iqueue_add_tail(&seg->node, &kcp->snd_lst.lst);
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
int ikcp_input(ikcpcb *kcp, char *data, long size)
{
	return -3;
}


//---------------------------------------------------------------------
// ikcp_encode_seg
//---------------------------------------------------------------------
static char *ikcp_encode_seg(char *ptr, const IKCPSEG *seg)
{
	return ptr + ARQ_OVERHEAD_SIZE;
}


//---------------------------------------------------------------------
// ikcp_flush
//---------------------------------------------------------------------
int ikcp_send_next(ikcpcb *kcp)
{
	IKCPSEG *seg;
	if (!iqueue_is_empty(&kcp->snd_lst.lst)) {
		seg = iqueue_entry(kcp->snd_lst.lst.next, IKCPSEG, node);

		int res = rp_udp_output(seg->data_buf - ARQ_OVERHEAD_SIZE, 0, kcp);
		if (res)
			return res;

		iqueue_del(&seg->node);
		ikcp_segment_delete(kcp, seg);
		--kcp->n_snd;
		return 0;
	}

	return -1;
}

int ikcp_wndsize(ikcpcb *kcp, int sndwnd)
{
	if (kcp) {
		if (sndwnd > 0) {
			kcp->n_snd_max = _imin_(sndwnd, SEND_BUFS_COUNT);
		}
	}
	return 0;
}

int ikcp_can_queue(const ikcpcb *kcp)
{
	return kcp->n_snd < kcp->n_snd_max;
}

int ikcp_can_send(const ikcpcb *kcp)
{
	return !iqueue_is_empty(&kcp->snd_lst.lst);
}
