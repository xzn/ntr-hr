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

#define IKCP_FASTACK_CONSERVE

#pragma GCC diagnostic ignored "-Wunused-parameter"
#pragma GCC diagnostic ignored "-Wunused-function"
#pragma GCC diagnostic ignored "-Wunused-variable"

//=====================================================================
// KCP BASIC
//=====================================================================
const IUINT32 IKCP_RTO_NDL = 25000;		// no delay min rto
const IUINT32 IKCP_RTO_MIN = 50000;		// normal min rto
const IUINT32 IKCP_RTO_DEF = 100000;
const IUINT32 IKCP_RTO_MAX = 2000000;
const IUINT32 IKCP_CMD_PUSH = 81;		// cmd: push data
const IUINT32 IKCP_CMD_ACK  = 82;		// cmd: ack
const IUINT32 IKCP_CMD_WASK = 83;		// cmd: window probe (ask)
const IUINT32 IKCP_CMD_WINS = 84;		// cmd: window size (tell)
const IUINT32 IKCP_ASK_SEND = 1;		// need to send IKCP_CMD_WASK
const IUINT32 IKCP_ASK_TELL = 2;		// need to send IKCP_CMD_WINS
const IUINT32 IKCP_WND_SND = IKCP_WND_SND_MAX;
const IUINT32 IKCP_WND_RCV = IKCP_WND_RCV_CONST;       // must >= max fragment size
const IUINT32 IKCP_ACK_FAST	= 3;
const IUINT32 IKCP_INTERVAL	= 100000;
const IUINT32 IKCP_OVERHEAD = IKCP_OVERHEAD_CONST;
const IUINT32 IKCP_DEADLINK = 20;
const IUINT32 IKCP_THRESH_INIT = 2;
const IUINT32 IKCP_THRESH_MIN = 2;
const IUINT32 IKCP_PROBE_INIT = 1000000;		// secs to probe window size
const IUINT32 IKCP_PROBE_LIMIT = 2000000;	// max secs to probe window
const IUINT32 IKCP_FASTACK_LIMIT = 5;		// max times to trigger fastack


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

// delete a segment
static void ikcp_recv_segment_delete(ikcpcb *kcp, IKCPSEG *seg)
{
	free_recv_seg_data_buf(seg->data_buf);
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

	iqueue_init(&kcp->snd_lst);
	iqueue_init(&kcp->wak_lst);
	kcp->n_snd = 0;
	kcp->n_wak = 0;
	kcp->n_snd_max = IKCP_WND_SND_MAX;

	int ret;

	ret = mp_init(
		IKCP_SEG_MEM_SIZE_CONST,
		IKCP_WND_SND_MAX,
		kcp->seg_mem,
		&kcp->seg_pool);
	if (ret != 0)
		return -1;

	return 0;
}


//---------------------------------------------------------------------
// user/upper level send, returns at/below zero for error
//---------------------------------------------------------------------
int ikcp_send(ikcpcb *kcp, char *buffer, int len)
{
	IKCPSEG *seg;

	if (len <= 0) return -1;

	if (len > PACKET_SIZE - IKCP_OVERHEAD_CONST) {
		return -2;
	}

	seg = ikcp_segment_new(kcp);
	if (seg == NULL) {
		return -3;
	}

	seg->data_buf = buffer;
	seg->cid = kcp->cid;
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
static void ikcp_parse_ack(ikcpcb *kcp, IUINT32 pid)
{
	struct IQUEUEHEAD *p, *next;
}

static void ikcp_parse_una(ikcpcb *kcp, IUINT32 pid)
{
	struct IQUEUEHEAD *p, *next;
}


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
	return 0;
}


//---------------------------------------------------------------------
// ikcp_encode_seg
//---------------------------------------------------------------------
static char *ikcp_encode_seg(char *ptr, const IKCPSEG *seg)
{
	return ptr + IKCP_OVERHEAD_CONST;
}


//---------------------------------------------------------------------
// ikcp_flush
//---------------------------------------------------------------------
int ikcp_flush(ikcpcb *kcp)
{
	void *data_ptr[ARP_PACKET_COUNT] = { kcp };

	char encoder_mem[fecal_encoder_size()];
	FecalEncoder encoder = (FecalEncoder)encoder_mem;

	if (fecal_encoder_init(encoder, ARP_PACKET_COUNT, data_ptr, sizeof(ikcpcb)) != Fecal_Success) {
		return -1;
	}

	char recovery_data[ARP_DATA_SIZE] = { 0 };
	FecalSymbol recovery = {
		.Data = recovery_data,
		.Bytes = ARP_DATA_SIZE,
		.Index = 0,
	};
	if (fecal_encode(encoder, &recovery) != Fecal_Success) {
		return -2;
	}

	IKCPSEG *seg;
	while (!iqueue_is_empty(&kcp->snd_lst)) {
		seg = iqueue_entry(kcp->snd_lst.next, IKCPSEG, node);
		iqueue_del(&seg->node);
		ikcp_segment_delete(kcp, seg);
		--kcp->n_snd;
	}

	return 0;
}


int ikcp_wndsize(ikcpcb *kcp, int sndwnd)
{
	if (kcp) {
		if (sndwnd > 0) {
			kcp->n_snd_max = _imin_(sndwnd, IKCP_WND_SND_MAX);
		}
	}
	return 0;
}

int ikcp_waitsnd(const ikcpcb *kcp)
{
	return kcp->n_snd + kcp->n_wak;
}

