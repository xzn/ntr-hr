#ifndef RP_JLS_H
#define RP_JLS_H

#include "rp_common.h"

enum {
	RP_ENCODE_PARAMS_BPP8,
	RP_ENCODE_PARAMS_BPP7,
	RP_ENCODE_PARAMS_BPP6,
	RP_ENCODE_PARAMS_BPP5,
	RP_ENCODE_PARAMS_BPP4,
	RP_ENCODE_PARAMS_COUNT
};

struct rp_jls_params_t {
	struct {
		uint16_t vLUT_bpp8[2 * (1 << 8)][3];
		uint16_t vLUT_bpp7[2 * (1 << 7)][3];
		uint16_t vLUT_bpp6[2 * (1 << 6)][3];
		uint16_t vLUT_bpp5[2 * (1 << 5)][3];
		uint16_t vLUT_bpp4[2 * (1 << 4)][3];
		int16_t classmap[9 * 9 * 9];
	} enc_luts;
	struct jls_enc_params enc_params[RP_ENCODE_PARAMS_COUNT];
};

struct rp_jls_ctx_t {
	struct jls_enc_ctx enc;
	struct bito_ctx bito;
};

enum rp_send_header_type {
	RP_SEND_HEADER_TYPE_CONF,
	RP_SEND_HEADER_TYPE_DATA,
};

struct rp_send_info_header {
    u32 type_conf : 1;
    u32 downscale_uv : 1;
    u32 yuv_option : 2;
    u32 color_transform_hp : 2;
    u32 me_enabled : 2;
    u32 me_downscale : 1;
    u32 me_search_param : 5;
    u32 me_block_size : 2;
    u32 me_interpolate : 1;
};

struct rp_send_data_header {
    u32 type_data : 1;
    u32 top_bot : 1;
    u32 frame_n : RP_IMAGE_FRAME_N_BITS;
    u32 p_frame : 1;
    u32 bpp : 3;
    u32 data_end : 1;
    u32 plane_type : 1;
    u32 plane_comp : 2;
};

_Static_assert(sizeof(struct rp_send_info_header) == sizeof(u32));
_Static_assert(sizeof(struct rp_send_data_header) == sizeof(u32));

struct rp_syn_comp_t;
struct rp_network_encode_t;
struct rp_net_state_t;
struct rp_jls_send_ctx_t {
	struct rp_send_data_header *send_header;
	struct rp_syn_comp_t *network_queue;
	volatile u8 *exit_thread;
	u8 multicore_network;
	u8 network_sync;
	u8 thread_n;
	struct rp_network_encode_t *network;
	struct rp_net_state_t *net_state;
	u8 *buffer_begin;
	u8 *buffer_end;
	int send_size_total;
};

enum rp_plane_type_t {
	RP_PLANE_TYPE_COLOR,
	RP_PLANE_TYPE_ME,
};

enum rp_plane_comp_t {
	RP_PLANE_COMP_Y,
	RP_PLANE_COMP_U,
	RP_PLANE_COMP_V,

	RP_PLANE_COMP_ME_X = 0,
	RP_PLANE_COMP_ME_Y,
};

void jls_encoder_prepare_LUTs(struct rp_jls_params_t *params);
int ffmpeg_jls_decode(uint8_t *dst, int width, int height, int pitch, const uint8_t *src, int src_size, int bpp);
int rpJLSEncodeImage(struct rp_jls_send_ctx_t *send_ctx,
	struct rp_jls_params_t *params, struct rp_jls_ctx_t *jls_ctx,
	const u8 *src, int w, int h, int bpp, u8 encoder_which);

#endif
