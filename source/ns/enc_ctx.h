// buffer
#define I_B_(N_, w, h) u8 N_[(w) * (h)] I_A

struct I_C(ENC_FD_CTX_im_, I_N) {
	I_B_(im, I_WIDTH, I_HEIGHT);
	I_B_(ds, I_WIDTH, I_HEIGHT / 2);
	I_B_(ds2, I_WIDTH / 2, I_HEIGHT / 2);
};

struct I_C(ENC_FD_CTX_ds2_, I_N) {
	I_B_(im, I_WIDTH / 2, I_HEIGHT / 2);
	I_B_(ds, I_WIDTH / 2, I_HEIGHT / 4);
	I_B_(ds2, I_WIDTH / 4, I_HEIGHT / 4);
};

struct I_C(ENC_FD_CTX_, I_N) {
	struct I_C(ENC_FD_CTX_im_, I_N) y;
	struct I_C(ENC_FD_CTX_ds2_, I_N) ds2_u, ds2_v;
	u8 flags I_A;
};

typedef I_B_(I_C(I_C(ENC_CTX_im_im_, I_N), _p), I_WIDTH, I_HEIGHT);
typedef I_B_(I_C(I_C(ENC_CTX_im_im_, I_N), _fd), I_WIDTH, I_HEIGHT);
typedef I_B_(I_C(I_C(ENC_CTX_im_im_, I_N), _s), I_WIDTH, I_HEIGHT);
typedef u8 I_C(I_C(ENC_CTX_im_im_, I_N), _m)[ENCODE_SELECT_MASK_SIZE(I_WIDTH, I_HEIGHT)] I_A;

typedef I_B_(I_C(I_C(ENC_CTX_ds_im_, I_N), _p), I_WIDTH, I_HEIGHT / 2);
typedef I_B_(I_C(I_C(ENC_CTX_ds_im_, I_N), _fd), I_WIDTH, I_HEIGHT / 2);
typedef I_B_(I_C(I_C(ENC_CTX_ds_im_, I_N), _s), I_WIDTH, I_HEIGHT / 2);
typedef u8 I_C(I_C(ENC_CTX_ds_im_, I_N), _m)[ENCODE_SELECT_MASK_SIZE(I_WIDTH, I_HEIGHT / 2)] I_A;

typedef I_B_(I_C(I_C(ENC_CTX_ds2_im_, I_N), _p), I_WIDTH / 2, I_HEIGHT / 2);
typedef I_B_(I_C(I_C(ENC_CTX_ds2_im_, I_N), _fd), I_WIDTH / 2, I_HEIGHT / 2);
typedef I_B_(I_C(I_C(ENC_CTX_ds2_im_, I_N), _s), I_WIDTH / 2, I_HEIGHT / 2);
typedef u8 I_C(I_C(ENC_CTX_ds2_im_, I_N), _m)[ENCODE_SELECT_MASK_SIZE(I_WIDTH / 2, I_HEIGHT / 2)] I_A;

typedef I_B_(I_C(I_C(ENC_CTX_ds_ds2_im_, I_N), _p), I_WIDTH / 2, I_HEIGHT / 4);
typedef I_B_(I_C(I_C(ENC_CTX_ds_ds2_im_, I_N), _fd), I_WIDTH / 2, I_HEIGHT / 4);
typedef I_B_(I_C(I_C(ENC_CTX_ds_ds2_im_, I_N), _s), I_WIDTH / 2, I_HEIGHT / 4);
typedef u8 I_C(I_C(ENC_CTX_ds_ds2_im_, I_N), _m)[ENCODE_SELECT_MASK_SIZE(I_WIDTH / 2, I_HEIGHT / 4)] I_A;

typedef I_B_(I_C(I_C(ENC_CTX_ds2_ds2_im_, I_N), _p), I_WIDTH / 4, I_HEIGHT / 4);
typedef I_B_(I_C(I_C(ENC_CTX_ds2_ds2_im_, I_N), _fd), I_WIDTH / 4, I_HEIGHT / 4);
typedef I_B_(I_C(I_C(ENC_CTX_ds2_ds2_im_, I_N), _s), I_WIDTH / 4, I_HEIGHT / 4);
typedef u8 I_C(I_C(ENC_CTX_ds2_ds2_im_, I_N), _m)[ENCODE_SELECT_MASK_SIZE(I_WIDTH / 4, I_HEIGHT / 4)] I_A;

typedef I_B_(I_C(I_C(ENC_CTX_ds_rs_, I_N), _fd), I_WIDTH, I_HEIGHT / 2);
typedef u8 I_C(I_C(ENC_CTX_ds_rs_, I_N), _c)[ENCODE_UPSAMPLE_CARRY_SIZE(I_WIDTH, I_HEIGHT / 2)] I_A;

typedef I_B_(I_C(I_C(ENC_CTX_ds2_rs_, I_N), _fd), I_WIDTH / 2, I_HEIGHT / 2);
typedef u8 I_C(I_C(ENC_CTX_ds2_rs_, I_N), _c)[ENCODE_UPSAMPLE_CARRY_SIZE(I_WIDTH / 2, I_HEIGHT / 2)] I_A;

typedef I_B_(I_C(I_C(ENC_CTX_ds_ds2_rs_, I_N), _fd), I_WIDTH / 2, I_HEIGHT / 4);
typedef u8 I_C(I_C(ENC_CTX_ds_ds2_rs_, I_N), _c)[ENCODE_UPSAMPLE_CARRY_SIZE(I_WIDTH / 2, I_HEIGHT / 4)] I_A;

typedef I_B_(I_C(I_C(ENC_CTX_ds2_ds2_rs_, I_N), _fd), I_WIDTH / 4, I_HEIGHT / 4);
typedef u8 I_C(I_C(ENC_CTX_ds2_ds2_rs_, I_N), _c)[ENCODE_UPSAMPLE_CARRY_SIZE(I_WIDTH / 4, I_HEIGHT / 4)] I_A;

struct I_C(ENC_CTX_im_, I_N) {
	I_B_(u, I_WIDTH, I_HEIGHT);
	I_B_(v, I_WIDTH, I_HEIGHT);
	I_C(I_C(ENC_CTX_im_im_, I_N), _p) y_p;
	I_C(I_C(ENC_CTX_im_im_, I_N), _fd) y_fd;
	I_C(I_C(ENC_CTX_im_im_, I_N), _s) y_s;
	I_C(I_C(ENC_CTX_im_im_, I_N), _m) y_m;
	I_C(I_C(ENC_CTX_ds2_im_, I_N), _p) ds2_u_p, ds2_v_p;
	I_C(I_C(ENC_CTX_ds2_im_, I_N), _fd) ds2_u_fd, ds2_v_fd;
	I_C(I_C(ENC_CTX_ds2_im_, I_N), _s) ds2_u_s, ds2_v_s;
	I_C(I_C(ENC_CTX_ds2_im_, I_N), _m) ds2_u_m, ds2_v_m;
	I_C(I_C(ENC_CTX_ds_im_, I_N), _p) ds_y_p;
	I_C(I_C(ENC_CTX_ds_im_, I_N), _fd) ds_y_fd;
	I_C(I_C(ENC_CTX_ds_im_, I_N), _s) ds_y_s;
	I_C(I_C(ENC_CTX_ds_im_, I_N), _m) ds_y_m;
	I_C(I_C(ENC_CTX_ds_ds2_im_, I_N), _p) ds_ds2_u_p, ds_ds2_v_p;
	I_C(I_C(ENC_CTX_ds_ds2_im_, I_N), _fd) ds_ds2_u_fd, ds_ds2_v_fd;
	I_C(I_C(ENC_CTX_ds_ds2_im_, I_N), _s) ds_ds2_u_s, ds_ds2_v_s;
	I_C(I_C(ENC_CTX_ds_ds2_im_, I_N), _m) ds_ds2_u_m, ds_ds2_v_m;
	I_C(I_C(ENC_CTX_ds2_im_, I_N), _p) ds2_y_p;
	I_C(I_C(ENC_CTX_ds2_im_, I_N), _fd) ds2_y_fd;
	I_C(I_C(ENC_CTX_ds2_im_, I_N), _s) ds2_y_s;
	I_C(I_C(ENC_CTX_ds2_im_, I_N), _m) ds2_y_m;
	I_C(I_C(ENC_CTX_ds2_ds2_im_, I_N), _p) ds2_ds2_u_p, ds2_ds2_v_p;
	I_C(I_C(ENC_CTX_ds2_ds2_im_, I_N), _fd) ds2_ds2_u_fd, ds2_ds2_v_fd;
	I_C(I_C(ENC_CTX_ds2_ds2_im_, I_N), _s) ds2_ds2_u_s, ds2_ds2_v_s;
	I_C(I_C(ENC_CTX_ds2_ds2_im_, I_N), _m) ds2_ds2_u_m, ds2_ds2_v_m;
};

struct I_C(ENC_CTX_rs_, I_N) {
	I_C(I_C(ENC_CTX_ds_rs_, I_N), _fd) ds_y_fd;
	I_C(I_C(ENC_CTX_ds_rs_, I_N), _c) ds_y_c;
	I_C(I_C(ENC_CTX_ds2_rs_, I_N), _fd) ds2_y_fd;
	I_C(I_C(ENC_CTX_ds2_rs_, I_N), _c) ds2_y_c;
	I_C(I_C(ENC_CTX_ds_ds2_rs_, I_N), _fd) ds_ds2_u_fd, ds_ds2_v_fd;
	I_C(I_C(ENC_CTX_ds_ds2_rs_, I_N), _c) ds_ds2_u_c, ds_ds2_v_c;
	I_C(I_C(ENC_CTX_ds2_ds2_rs_, I_N), _fd) ds2_ds2_u_fd, ds2_ds2_v_fd;
	I_C(I_C(ENC_CTX_ds2_ds2_rs_, I_N), _c) ds2_ds2_u_c, ds2_ds2_v_c;
};

struct I_C(ENC_CTX_, I_N) {
	struct I_C(ENC_CTX_im_, I_N) im;
	struct I_C(ENC_CTX_rs_, I_N) rs;
};

#undef I_B_
