#include "rp_conf.h"

union rp_conf_arg0_t {
	int arg0;
	struct {
		u32 kcp_nocwnd : 1;
		u32 kcp_fastresend : 2;
		u32 me_select : RP_IMAGE_ME_SELECT_BITS;
		u32 multicore_network : 1;
		u32 multicore_screen : 1;
	};
};

union rp_conf_arg1_t {
	int arg1;
	struct {
		u32 yuv_option : 2;
		u32 color_transform_hp : 2;
		u32 downscale_uv : 1;
		u32 encoder_which : 2;
		u32 me_block_size : 2;
		u32 me_method : 3;
		u32 me_search_param : 5;
		u32 me_downscale : 1;
		u32 me_interpolate : 1;
		u32 kcp_minrto : 7;
		u32 kcp_snd_wnd_size : 6;
	};
};

union rp_conf_arg2_t {
	int arg2;
	struct {
		u32 top_priority : 4;
		u32 bot_priority : 4;
		u32 low_latency : 1;
		u32 multicore_encode : 1;
		u32 target_mbit_rate : 5;
		u32 dynamic_priority : 1;
		u32 min_dp_frame_rate : 7;
		u32 max_frame_rate : 7;
		u32 kcp_nodelay : 2;
	};
};

_Static_assert(sizeof(union rp_conf_arg0_t) == sizeof(int));
_Static_assert(sizeof(union rp_conf_arg1_t) == sizeof(int));
_Static_assert(sizeof(union rp_conf_arg2_t) == sizeof(int));

int rp_set_params(struct rp_conf_t *conf) {
	union rp_conf_arg0_t arg0 = { .arg0 = g_nsConfig->startupInfo[8] };
	union rp_conf_arg1_t arg1 = { .arg1 = g_nsConfig->startupInfo[9] };
	union rp_conf_arg2_t arg2 = { .arg2 = g_nsConfig->startupInfo[10] };

	conf->updated = 0;

	conf->kcp.conv = RP_KCP_MAGIC;
	conf->kcp.fastresend = arg0.kcp_fastresend;
	conf->kcp.nocwnd = arg0.kcp_nocwnd;
	conf->kcp.minrto = arg1.kcp_minrto + RP_KCP_MIN_MINRTO;
	conf->kcp.snd_wnd_size = arg1.kcp_snd_wnd_size + RP_KCP_MIN_SNDWNDSIZE;
	conf->kcp.nodelay = arg2.kcp_nodelay;

	conf->yuv_option = arg1.yuv_option;
	conf->color_transform_hp = arg1.color_transform_hp;
	conf->downscale_uv = arg1.downscale_uv;
	conf->encoder_which = arg1.encoder_which;

	conf->me.select = arg0.me_select;
	conf->me.method = arg1.me_method;
	conf->me.enabled = conf->me.method > 1 ? 1 : conf->me.method == 1 ? -1 : 0;
	conf->me.block_size = RP_ME_MIN_BLOCK_SIZE << arg1.me_block_size;
	conf->me.block_size_log2 = av_ceil_log2(conf->me.block_size);
	conf->me.search_param = arg1.me_search_param + RP_ME_MIN_SEARCH_PARAM;
	conf->me.bpp = conf->me.enabled > 1 && conf->me.select ? 1 :
		conf->me.enabled == 1 ? av_ceil_log2(conf->me.search_param * 2 + 1) : 0;
	conf->me.bpp_half_range = (1 << conf->me.bpp) >> 1;
	conf->me.mafd_shift = RP_MAX(0, (int)conf->me.block_size_log2 * 2 - 8);
	conf->me.select_threshold =
		((u32)conf->me.block_size * (u32)conf->me.block_size * (u32)conf->me.select) >> conf->me.mafd_shift;
	conf->me.downscale = arg1.me_downscale;
#if RP_ME_INTERPOLATE
	conf->me.interpolate = arg1.me_interpolate;
#else
	conf->me.interpolate = 0;
#endif

	conf->screen_priority[SCREEN_TOP] = arg2.top_priority;
	conf->screen_priority[SCREEN_BOT] = arg2.bot_priority;
	conf->low_latency = arg2.low_latency;
	if (RP_ENCODE_MULTITHREAD)
		conf->multicore_encode = arg2.multicore_encode;
	else
		conf->multicore_encode = 0;
	conf->multicore_network = arg0.multicore_network;
	conf->multicore_screen = arg0.multicore_screen;
	conf->dynamic_priority = arg2.dynamic_priority;
	conf->min_dp_frame_rate = arg2.min_dp_frame_rate;
	conf->max_frame_rate = arg2.max_frame_rate;
	conf->target_mbit_rate = arg2.target_mbit_rate;

	conf->encode_buffer_count = RP_ENCODE_BUFFER_COUNT - conf->low_latency -
		(RP_ENCODE_MULTITHREAD && !conf->multicore_encode);

	conf->min_send_interval_ticks =
		(u64)SYSTICK_PER_SEC * NWM_PACKET_SIZE * 8 /
		((u16)conf->target_mbit_rate + 1) / 1024 / 1024;

	conf->min_capture_interval_ticks = conf->max_frame_rate ?
		(u64)SYSTICK_PER_SEC / (u16)conf->max_frame_rate : 0;

	nsDbgPrint(
		"yuv option: %d\n"
		"color transform hp: %d\n"
		"downscale uv: %d\n"
		"encoder which: %d\n"
		"me method: %d\n"
		"me select: %d\n"
		"me enabled: %d\n"
		"me block size: %d\n"
		"me block size log2: %d\n"
		"me search param: %d\n"
		"me search param bpp: %d\n"
		"me search param bpp half range: %d\n"
		"me mafd shift: %d\n"
		"me select threshold: %d\n"
		"me downscale: %d\n"
		"me interpolate: %d\n"
		"top screen priority: %d\n"
		"bot screen priority: %d\n"
		"multicore encode: %d\n"
		"multicore network: %d\n"
		"multicore screen: %d\n"
		"dynamic priority: %d\n"
		"min dp frame rate: %d\n"
		"max frame rate: %d\n"
		"target mbit rate: %d\n"
		"encode buffer count: %d\n"
		"min send interval ticks: %d\n"
		"min capture interval ticks: %d\n",
		conf->yuv_option,
		conf->color_transform_hp,
		conf->downscale_uv,
		conf->encoder_which,
		conf->me.select,
		conf->me.method,
		conf->me.enabled,
		conf->me.block_size,
		conf->me.block_size_log2,
		conf->me.search_param,
		conf->me.bpp,
		conf->me.bpp_half_range,
		conf->me.mafd_shift,
		conf->me.select_threshold,
		conf->me.downscale,
		conf->me.interpolate,
		conf->screen_priority[SCREEN_TOP],
		conf->screen_priority[SCREEN_BOT],
		conf->multicore_encode,
		conf->multicore_network,
		conf->multicore_screen,
		conf->dynamic_priority,
		conf->min_dp_frame_rate,
		conf->max_frame_rate,
		conf->target_mbit_rate,
		conf->encode_buffer_count,
		conf->min_send_interval_ticks,
		conf->min_capture_interval_ticks
		);

	return 0;
}

int rp_check_params(struct rp_conf_t *conf, volatile u32 *updated, volatile u8 *exit_thread) {
	if (*updated) {
		*updated = 0;
		conf->updated = 1;
		*exit_thread = 1;
	}

	return 0;
}
