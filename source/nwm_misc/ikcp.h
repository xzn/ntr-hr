// Sending end of the Reliable Stream protocol
// Based on ideas from skywind3000 kcp but redesigned and reimplemented from scratch
// Under MIT license (see end of file)

// Allows one way communication only
// Only tested for use in NTRViewer-HR

#ifndef __IKCP_H__
#define __IKCP_H__

#include <stddef.h>

//=====================================================================
// 32BIT INTEGER DEFINITION
//=====================================================================
#ifndef __INTEGER_32_BITS__
#define __INTEGER_32_BITS__
#if defined(_WIN64) || defined(WIN64) || defined(__amd64__) || \
	defined(__x86_64) || defined(__x86_64__) || defined(_M_IA64) || \
	defined(_M_AMD64)
	typedef unsigned int ISTDUINT32;
	typedef int ISTDINT32;
#elif defined(_WIN32) || defined(WIN32) || defined(__i386__) || \
	defined(__i386) || defined(_M_X86)
	typedef unsigned long ISTDUINT32;
	typedef long ISTDINT32;
#elif defined(__MACOS__)
	typedef UInt32 ISTDUINT32;
	typedef SInt32 ISTDINT32;
#elif defined(__APPLE__) && defined(__MACH__)
	#include <sys/types.h>
	typedef u_int32_t ISTDUINT32;
	typedef int32_t ISTDINT32;
#elif defined(__BEOS__)
	#include <sys/inttypes.h>
	typedef u_int32_t ISTDUINT32;
	typedef int32_t ISTDINT32;
#elif (defined(_MSC_VER) || defined(__BORLANDC__)) && (!defined(__MSDOS__))
	typedef unsigned __int32 ISTDUINT32;
	typedef __int32 ISTDINT32;
#elif defined(__GNUC__)
	#include <stdint.h>
	typedef uint32_t ISTDUINT32;
	typedef int32_t ISTDINT32;
#else
	typedef unsigned long ISTDUINT32;
	typedef long ISTDINT32;
#endif
#endif


//=====================================================================
// Integer Definition
//=====================================================================
#ifndef __IINT8_DEFINED
#define __IINT8_DEFINED
typedef char IINT8;
#endif

#ifndef __IUINT8_DEFINED
#define __IUINT8_DEFINED
typedef unsigned char IUINT8;
#endif

#ifndef __IUINT16_DEFINED
#define __IUINT16_DEFINED
typedef unsigned short IUINT16;
#endif

#ifndef __IINT16_DEFINED
#define __IINT16_DEFINED
typedef short IINT16;
#endif

#ifndef __IINT32_DEFINED
#define __IINT32_DEFINED
typedef ISTDINT32 IINT32;
#endif

#ifndef __IUINT32_DEFINED
#define __IUINT32_DEFINED
typedef ISTDUINT32 IUINT32;
#endif

#ifndef __IINT64_DEFINED
#define __IINT64_DEFINED
#if defined(_MSC_VER) || defined(__BORLANDC__)
typedef __int64 IINT64;
#else
typedef long long IINT64;
#endif
#endif

#ifndef __IUINT64_DEFINED
#define __IUINT64_DEFINED
#if defined(_MSC_VER) || defined(__BORLANDC__)
typedef unsigned __int64 IUINT64;
#else
typedef unsigned long long IUINT64;
#endif
#endif

#ifndef INLINE
#if defined(__GNUC__)

#if (__GNUC__ > 3) || ((__GNUC__ == 3) && (__GNUC_MINOR__ >= 1))
#define INLINE         __inline__ __attribute__((always_inline))
#else
#define INLINE         __inline__
#endif

#elif (defined(_MSC_VER) || defined(__BORLANDC__) || defined(__WATCOMC__))
#define INLINE __inline
#else
#define INLINE
#endif
#endif

#if (!defined(__cplusplus)) && (!defined(inline))
#define inline INLINE
#endif


//=====================================================================
// QUEUE DEFINITION
//=====================================================================
#ifndef __IQUEUE_DEF__
#define __IQUEUE_DEF__

struct IQUEUEHEAD {
	struct IQUEUEHEAD *next, *prev;
};

typedef struct IQUEUEHEAD iqueue_head;


//---------------------------------------------------------------------
// queue init
//---------------------------------------------------------------------
#define IQUEUE_HEAD_INIT(name) { &(name), &(name) }
#define IQUEUE_HEAD(name) \
	struct IQUEUEHEAD name = IQUEUE_HEAD_INIT(name)

#define IQUEUE_INIT(ptr) ( \
	(ptr)->next = (ptr), (ptr)->prev = (ptr))

#define IOFFSETOF(TYPE, MEMBER) ((size_t) &((TYPE *)0)->MEMBER)

#define ICONTAINEROF(ptr, type, member) ( \
		(type*)( ((char*)((type*)ptr)) - IOFFSETOF(type, member)) )

#define IQUEUE_ENTRY(ptr, type, member) ICONTAINEROF(ptr, type, member)


//---------------------------------------------------------------------
// queue operation
//---------------------------------------------------------------------
#define IQUEUE_ADD(node, head) ( \
	(node)->prev = (head), (node)->next = (head)->next, \
	(head)->next->prev = (node), (head)->next = (node))

#define IQUEUE_ADD_TAIL(node, head) ( \
	(node)->prev = (head)->prev, (node)->next = (head), \
	(head)->prev->next = (node), (head)->prev = (node))

#define IQUEUE_DEL_BETWEEN(p, n) ((n)->prev = (p), (p)->next = (n))

#define IQUEUE_DEL(entry, g) (\
	(entry)->next->prev = (entry)->prev, \
	(entry)->prev->next = (entry)->next, \
	(entry)->next = (struct IQUEUEHEAD *)(g), (entry)->prev = (struct IQUEUEHEAD *)(g))

#define IQUEUE_DEL_INIT(entry) do { \
	IQUEUE_DEL(entry, entry); } while (0)

#define IQUEUE_IS_EMPTY(entry) ((entry) == (entry)->next)

#define iqueue_init     IQUEUE_INIT
#define iqueue_entry    IQUEUE_ENTRY
#define iqueue_add      IQUEUE_ADD
#define iqueue_add_tail IQUEUE_ADD_TAIL
#define iqueue_del      IQUEUE_DEL
#define iqueue_del_init IQUEUE_DEL_INIT
#define iqueue_is_empty IQUEUE_IS_EMPTY

#define iqueue_ins_before iqueue_add_tail
#define iqueue_ins_after  iqueue_add

#define IQUEUE_FOREACH(iterator, head, TYPE, MEMBER) \
	for ((iterator) = iqueue_entry((head)->next, TYPE, MEMBER); \
		&((iterator)->MEMBER) != (head); \
		(iterator) = iqueue_entry((iterator)->MEMBER.next, TYPE, MEMBER))

#define iqueue_foreach(iterator, head, TYPE, MEMBER) \
	IQUEUE_FOREACH(iterator, head, TYPE, MEMBER)

#define iqueue_foreach_entry(pos, head) \
	for( (pos) = (head)->next; (pos) != (head) ; (pos) = (pos)->next )


#define __iqueue_splice(list, head) do {	\
		iqueue_head *first = (list)->next, *last = (list)->prev; \
		iqueue_head *at = (head)->next; \
		(first)->prev = (head), (head)->next = (first);		\
		(last)->next = (at), (at)->prev = (last); }	while (0)

#define iqueue_splice(list, head) do { \
	if (!iqueue_is_empty(list)) __iqueue_splice(list, head); } while (0)

#define iqueue_splice_init(list, head) do {	\
	iqueue_splice(list, head);	iqueue_init(list); } while (0)


#define __iqueue_splice_tail(list, head) do {	\
		iqueue_head *first = (list)->next, *last = (list)->prev; \
		iqueue_head *at = (head)->prev; \
		(first)->prev = (at), (at)->next = (first);		\
		(last)->next = (head), (head)->prev = (last); }	while (0)

#define iqueue_splice_tail(list, head) do { \
	if (!iqueue_is_empty(list)) __iqueue_splice_tail(list, head); } while (0)

#define iqueue_splice_tail_init(list, head) do {	\
	iqueue_splice_tail(list, head);	iqueue_init(list); } while (0)


#ifdef _MSC_VER
#pragma warning(disable:4311)
#pragma warning(disable:4312)
#pragma warning(disable:4996)
#endif

#endif


//---------------------------------------------------------------------
// BYTE ORDER & ALIGNMENT
//---------------------------------------------------------------------
#ifndef IWORDS_BIG_ENDIAN
	#ifdef _BIG_ENDIAN_
		#if _BIG_ENDIAN_
			#define IWORDS_BIG_ENDIAN 1
		#endif
	#endif
	#ifndef IWORDS_BIG_ENDIAN
		#if defined(__hppa__) || \
			defined(__m68k__) || defined(mc68000) || defined(_M_M68K) || \
			(defined(__MIPS__) && defined(__MIPSEB__)) || \
			defined(__ppc__) || defined(__POWERPC__) || defined(_M_PPC) || \
			defined(__sparc__) || defined(__powerpc__) || \
			defined(__mc68000__) || defined(__s390x__) || defined(__s390__)
			#define IWORDS_BIG_ENDIAN 1
		#endif
	#endif
	#ifndef IWORDS_BIG_ENDIAN
		#define IWORDS_BIG_ENDIAN  0
	#endif
#endif

#ifndef IWORDS_MUST_ALIGN
	#if defined(__i386__) || defined(__i386) || defined(_i386_)
		#define IWORDS_MUST_ALIGN 0
	#elif defined(_M_IX86) || defined(_X86_) || defined(__x86_64__)
		#define IWORDS_MUST_ALIGN 0
	#elif defined(__amd64) || defined(__amd64__)
		#define IWORDS_MUST_ALIGN 0
	#else
		#define IWORDS_MUST_ALIGN 1
	#endif
#endif


//=====================================================================
// SEGMENT
//=====================================================================
#include "ui.h"
#include "mempool.h"
#include "fecal.h"

#define FID_NBITS (12)
#define FTY_NBITS (2)
#define GID_NBITS (2)

#define PID_NBITS (12)
#define CID_NBITS (1)

#define count_nbits (sizeof(IUINT16) * 8 - PID_NBITS)

#define RSND_NBITS 2
#define RSND_COUNT 3

// #define CHECK_PID

extern FecalEncoder rp_kcp_fecal_encoder;

struct IKCPSEG
{
	struct IQUEUEHEAD node;
	char *data_buf;

	IUINT16 pid; // packet id
	IUINT16 fid; // fec packet group id
	IUINT16 wsn; // wait send count

	IUINT8 wrn : RSND_NBITS; // wait resend count
	IUINT8 fty : FTY_NBITS; // fec type
	IUINT8 gid : GID_NBITS; // id within fec packet group

	// use flags instead of doing conditions later
	IUINT8 recovery_data : 1;
	IUINT8 weak_data : 1;
	IUINT8 is_kcp_seg_data : 1;
	IUINT8 is_term_seg_data : 1;
	IUINT8 is_audio_seg_data : 1;
	IUINT8 term_notify : 1;
	IUINT8 gid_end : 1;

	IUINT32 send_time; // sys tick of when the segment was sent
};

const unsigned NWM_PACKET_SIZE = ROUND_UP(PACKET_SIZE + NWM_HDR_SIZE, sizeof(void *));
const unsigned RP_RECV_PACKET_SIZE = ROUND_UP(PACKET_SIZE, sizeof(void *));

const unsigned ARQ_BUFS_COUNT = ARQ_PREFERRED_COUNT_MAX;
const unsigned ARQ_CUR_BUFS_COUNT = ARQ_CUR_COUNT_MAX;
const unsigned RP_ARQ_ENCODE_BUFS_COUNT = RP_ARQ_ENCODE_COUNT_MAX;
const unsigned RP_ARQ_EXTRA_BUFS_COUNT = RP_ARQ_EXTRA_COUNT_MAX;
const unsigned SEND_BUFS_COUNT = SEND_BUFS_DATA_COUNT;
const unsigned SEND_BUFS_SIZE = SEND_BUFS_DATA_COUNT * NWM_PACKET_SIZE;
const unsigned SEND_CUR_BUFS_COUNT = ARQ_CUR_COUNT_MAX_2;
#define ARQ_SEG_MEM_COUNT (ARQ_PREFERRED_COUNT_MAX + ARQ_PREFERRED_COUNT_MAX_2)

//---------------------------------------------------------------------
// IKCPCB
//---------------------------------------------------------------------
struct IKCPCC {
	IUINT32 curr_send_time;
	IUINT32 last_send_time;
	IUINT32 curr_ack_count, curr_nack_count; // FIX16 format
	IUINT32 last_ack_count, last_nack_count; // FIX16 format
	IUINT32 avg_ack_count, avg_nack_count; // FIX16 format
	IUINT32 avg_dur; // ticks
};

struct IKCPCB
{
	IUINT16 cid; // conversation id
	IUINT16 pid; // next packet id
	IUINT16 fid; // next fec packet group id

	IUINT16 n_snd;
	IUINT16 n_snd_max;
	IUINT16 n_cur_max;

	struct IQUEUEHEAD snd_lst;
	struct IQUEUEHEAD rsnd_lsts[RSND_COUNT];
	struct IQUEUEHEAD snd_cur;
	struct IQUEUEHEAD snd_wak;

	struct IKCPCC congc;

	char seg_mem[ARQ_SEG_MEM_COUNT][ARQ_SEG_SIZE] ALIGNED(sizeof(void *));
	mp_pool_t seg_pool;

#ifdef CHECK_PID
	struct BitSet4096Mem pid_bs;
#endif
	// Worst case between every other packet is nack
	struct {
		u16 start, count;
	} nacks[ARQ_PREFERRED_COUNT_MAX];
	u16 n_nacks;

	bool session_established;
	bool session_new_data_received;
	bool rp_output_retry;
};

typedef struct IKCPCB ikcpcb;
typedef struct IKCPCC ikcpcc;

#ifdef __cplusplus
extern "C" {
#endif

extern char *ikcp_seg_data_buf_malloc(void);
extern void ikcp_seg_data_buf_free(const char *data_buf);

extern bool rp_audio_data_buf_free(const char *data_buf);
extern bool rp_term_data_buf_free(const char *data_buf);
extern void rp_seg_data_buf_free(const char *data_buf);
extern int rp_udp_output(char *buf, int len, u32 *tick, ikcpcb *kcp);
extern void rp_term_notify(void);
extern void rp_set_qos(u32 qos);
extern u32 rp_max_qos;
extern u32 rp_current_qos;

//---------------------------------------------------------------------
// interface
//---------------------------------------------------------------------

// create a new kcp control object, 'conv' must equal in two endpoint
// from the same connection. 'user' will be passed to the output callback
// output callback can be setup like this: 'kcp->output = my_udp_output'
int ikcp_create(ikcpcb* kcp, IUINT16 cid);

// user/upper level send
int ikcp_queue(ikcpcb *kcp, char *buffer, int len);

// when you received a low level packet (eg. UDP packet), call it
int ikcp_input(ikcpcb *kcp, char *data, int size);

// flush pending data
int ikcp_send_next(ikcpcb *kcp);

// set maximum window size
int ikcp_wndsize(ikcpcb *kcp, int sndwnd, int curwnd);

// get how many packet is waiting to be sent
int ikcp_queue_get_free(ikcpcb *kcp);
int ikcp_send_ready_and_get_delay(ikcpcb *kcp);


#ifdef __cplusplus
}
#endif

#endif

// MIT License

// Copyright (c) 2025

// Permission is hereby granted, free of charge, to any person obtaining a copy
// of this software and associated documentation files (the "Software"), to deal
// in the Software without restriction, including without limitation the rights
// to use, copy, modify, merge, publish, distribute, sublicense, and/or sell
// copies of the Software, and to permit persons to whom the Software is
// furnished to do so, subject to the following conditions:

// The above copyright notice and this permission notice shall be included in all
// copies or substantial portions of the Software.

// THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
// IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
// FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE
// AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
// LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM,
// OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE
// SOFTWARE.
