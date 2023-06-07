#ifndef RP_CONF_H
#define RP_CONF_H

#include "rp_common.h"

struct rp_conf_t {
    u8 updated;

    struct rp_conf_kcp_t {
        u32 conv;
        u8 fastresend;
        u8 nocwnd;
        u8 snd_wnd_size;
        u8 minrto;
        u8 nodelay;
    } kcp;

    u8 yuv_option;
    u8 color_transform_hp;
    u8 encoder_which;
    u8 downscale_uv;
    u8 encode_verify;

    struct rp_conf_me_t {
        u8 enabled;
        u8 select; // detect scene change
        u16 select_threshold;
        u8 mafd_shift;
        u8 method;
        u8 block_size;
        u8 block_size_log2;
        u8 search_param;
        u8 bpp;
        u8 bpp_half_range;
        u8 downscale;
        u8 interpolate;
    } me;

    u8 min_dp_frame_rate;
    u8 max_frame_rate;
    u8 target_mbit_rate;
    u8 dynamic_priority;
    u8 screen_priority[SCREEN_MAX];
    u8 low_latency;
    u8 multicore_encode;
    u8 multicore_network;
    u8 multicore_screen;
    u8 encode_buffer_count;

    u32 min_send_interval_ticks;
    u32 max_capture_interval_ticks;
    u32 min_capture_interval_ticks;
};

int rp_set_params(struct rp_conf_t *conf);
int rp_check_params(struct rp_conf_t *conf, volatile u8 *exit_thread);

#endif
