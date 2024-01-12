#include "global.h"

extern PLGLOADER_INFO *g_plgInfo;

#define NIGHT_SHIFT_RGB565_B(level)  \
            pix = *((u32*)(sp)); \
            pix = (pix & 0xffe0ffe0) | (((pix & 0x001f001f) >> level) & 0x001f001f); \
            *((u32*)(sp)) = pix; \
			sp += 2; 

#define NIGHT_SHIFT_RGB888_B(level)  \
            pix = *((u32*)(sp)); \
            pix = (pix & 0x00ffff00) | (((pix & 0xff0000ff) >> level) & 0xff0000ff); \
            *((u32*)(sp)) = pix; \
			sp += 4;  \
            sp[2] >>= level; \
			sp += 4;  \
            sp[1] >>= level; \
			sp += 4;  

#define NIGHT_SHIFT_RGB565_B_G(levelb, levelg)  \
            pix = *((u32*)(sp)); \
            pix = (pix & 0xf800f800) | (((pix & 0x07e007e0) >> levelg) & 0x07e007e0) | (((pix & 0x001f001f) >> levelb) & 0x001f001f) ; \
            *((u32*)(sp)) = pix; \
			sp += 2; 

#define REPEAT_8(a) a;a;a;a;a;a;a;a;
#define REPEAT_4(a) a;a;a;a;
#define REPEAT_6(a) a;a;a;a;a;a;

#define NIGHT_SHIFT_RGB565_B_G_UNALIGNED(levelb, levelg)  \
            pix = *sp; \
			b = (pix & 0x1f); \
			g = ((pix >> 5) & 0x3f); \
			pix &= 0xf800; \
			pix |= b >> levelb; \
			pix |= (g >> levelg) << 5; \
			*sp = pix; \
			sp++; 

#define NIGHT_SHIFT_RGB565_B_UNALIGNED(levelb)  \
            pix = *sp; \
			b = (pix & 0x1f); \
			pix &= 0xffe0; \
			pix |= b >> levelb; \
			*sp = pix; \
			sp++; 

#define NIGHT_SHIFT_RGB888_B_G_UNALIGNED(levelb, levelg)  \
			sp[0] >>= levelb; \
			sp[1] >>= levelg; \
			sp += 3; \
			sp[0] >>= levelb; \
			sp[1] >>= levelg; \
			sp += 3; \
			sp[0] >>= levelb; \
			sp[1] >>= levelg; \
			sp += 3; \
			sp[0] >>= levelb; \
			sp[1] >>= levelg; \
			sp += 3; 


#define INVERT_RGB888_UNALIGNED()  \
	*((u32*)sp) = 0xffffffffUL - *((u32*)sp); \
	sp += 4; \
	*((u32*)sp) = 0xffffffffUL - *((u32*)sp); \
	sp += 4; \
	*((u32*)sp) = 0xffffffffUL - *((u32*)sp); \
	sp += 4; 


#define INVERT_RGB565_UNALIGNED()  \
	*((u32*)sp) = 0xffffffffUL - *((u32*)sp); \
	sp += 2; 

#define GRAYSCALE_RGB888_UNALIGNED()  \
	gray = (u16)(sp[0] + sp[1] + sp[2]) / 3; \
	sp[0] = gray; \
	sp[1] = gray; \
	sp[2] = gray; \
	sp += 3; 

#define GRAYSCALE_RGB565_UNALIGNED()  \
    pix = *sp; \
	gray = (u16)((pix & 0x1f) + ((pix >> 6) & 0x1f) + ((pix >> 11) & 0x1f)) / 3; \
	*sp = gray | (gray << 6) | (gray << 11); \
	sp++; 

/*
BGRB GRBG RBGR
*/

/*
#define NIGHT_SHIFT_TEMPLATE_B(name, level) static u32 name(u32 addr, u32 stride, u32 height, u32 format) { \
	format &= 0x0f; \
    if (addr % 4 != 0) return 1; \
    u32 r; \
	if (format == 2) { \
        for (r = 0; r < height; r++) { \
            u16* sp = (u16*)addr; \
            u16* spEnd = (u16*)(addr + 240 * 2); \
            while (sp < spEnd) { \
                u32 pix; \
                REPEAT_8(NIGHT_SHIFT_RGB565_B(level)) \
            } \
            addr += stride; \
        } \
	} \
	else if (format == 1) { \
        for (r = 0; r < height; r++) { \
            u8* sp = (u8*)addr; \
            u8* spEnd = (u8*)(addr + 240 * 3); \
            while (sp < spEnd) { \
                u32 pix; \
                REPEAT_4(NIGHT_SHIFT_RGB888_B(level)) \
            } \
            addr += stride; \
        } \
	} \
	svc_flushProcessDataCache(0xffff8001, (u32)addr, stride * height); \
	return 0; \
}

#define NIGHT_SHIFT_TEMPLATE_B_G(name, levelb, levelg) static u32 name(u32 addr, u32 stride, u32 height, u32 format) { \
	format &= 0x0f; \
    if (addr % 4 != 0) return 1; \
    u32 r; \
	if (format == 2) { \
        for (r = 0; r < height; r++) { \
            u16* sp = (u16*)addr; \
            u16* spEnd = (u16*)(addr + 240 * 2); \
            while (sp < spEnd) { \
                u32 pix; \
                REPEAT_8(NIGHT_SHIFT_RGB565_B_G(levelb, levelg)) \
            } \
            addr += stride; \
        } \
	} \
	else if (format == 1) { \
        for (r = 0; r < height; r++) { \
            u8* sp = (u8*)addr; \
            u8* spEnd = (u8*)(addr + 240 * 3); \
            while (sp < spEnd) { \
                u32 pix; \
                REPEAT_4(NIGHT_SHIFT_RGB888_B_G_UNALIGNED(levelb, levelg)); \
            } \
            addr += stride; \
        } \
	} \
	svc_flushProcessDataCache(CURRENT_PROCESS_HANDLE, (u32)addr, stride * height); \
	return 0; \
}

NIGHT_SHIFT_TEMPLATE_B_G(plgNightShiftFramebufferLevel5, 4, 1);
NIGHT_SHIFT_TEMPLATE_B_G(plgNightShiftFramebufferLevel4, 3, 1);
NIGHT_SHIFT_TEMPLATE_B(plgNightShiftFramebufferLevel3, 3);
NIGHT_SHIFT_TEMPLATE_B(plgNightShiftFramebufferLevel2, 2);
NIGHT_SHIFT_TEMPLATE_B(plgNightShiftFramebufferLevel1, 1);*/

static inline __attribute__((always_inline)) u32 plgNightShift3(u32 addr, u32 stride, u32 height, u32 format, u32 levelb, u32 levelg, u32 isAligned) { 
	format &= 0x0f; 
    u32 r; 
	if (format == 2) { 
        for (r = 0; r < height; r++) { 
            u16* sp = (u16*)addr; 
            u16* spEnd = (u16*)(addr + 240 * 2); 
            while (sp < spEnd) { 
                
                if (isAligned) {
                    if (levelg) {
                        u32 pix;
                        REPEAT_8(NIGHT_SHIFT_RGB565_B_G(levelb, levelg)); 
                    } else {
                        u32 pix;
                        REPEAT_8(NIGHT_SHIFT_RGB565_B(levelb)); 
                    }
                } else {
                    if (levelg) {
                        u16 pix, b, g;
                        REPEAT_8(NIGHT_SHIFT_RGB565_B_G_UNALIGNED(levelb, levelg));
                        REPEAT_8(NIGHT_SHIFT_RGB565_B_G_UNALIGNED(levelb, levelg));
                    } else {
                        u16 pix, b;
                        REPEAT_8(NIGHT_SHIFT_RGB565_B_UNALIGNED(levelb));
                        REPEAT_8(NIGHT_SHIFT_RGB565_B_UNALIGNED(levelb));
                    }
                }

            } 
            addr += stride; 
        } 
	} 
	else if (format == 1) { 
        for (r = 0; r < height; r++) { 
            u8* sp = (u8*)addr; 
            u8* spEnd = (u8*)(addr + 240 * 3); 
            while (sp < spEnd) { 
                if (isAligned && (levelg == 0)) {
                    u32 pix;
                    REPEAT_4(NIGHT_SHIFT_RGB888_B(levelb));
                } else {
                    REPEAT_4(NIGHT_SHIFT_RGB888_B_G_UNALIGNED(levelb, levelg));
                }
                
            } 
            addr += stride; 
        } 
	} 
	svc_flushProcessDataCache(CURRENT_PROCESS_HANDLE, (u32)addr, stride * height); 
	return 0; 
}

static inline __attribute__((always_inline)) u32 plgScreenFilter(u32 addr, u32 stride, u32 height, u32 format, u32 type) {
	format &= 0x0f;
	u32 r;
	if (format == 2) {
		for (r = 0; r < height; r++) {
			u16* sp = (u16*)addr;
			u16* spEnd = (u16*)(addr + 240 * 2);
			while (sp < spEnd) {
				u16 pix, gray;
				if (type == 1) {
					REPEAT_4(INVERT_RGB565_UNALIGNED());
					REPEAT_4(INVERT_RGB565_UNALIGNED());
					REPEAT_4(INVERT_RGB565_UNALIGNED());
				}
				else {
					REPEAT_8(GRAYSCALE_RGB565_UNALIGNED());
					REPEAT_8(GRAYSCALE_RGB565_UNALIGNED());
					REPEAT_8(GRAYSCALE_RGB565_UNALIGNED());
				}

			}
			addr += stride;
		}
	}
	else if (format == 1) {
		for (r = 0; r < height; r++) {
			u8* sp = (u8*)addr;
			u8* spEnd = (u8*)(addr + 240 * 3);
			while (sp < spEnd) {
				u16 gray;
				if (type == 1) {
					REPEAT_4(INVERT_RGB888_UNALIGNED());
				}
				else {
					REPEAT_8(GRAYSCALE_RGB888_UNALIGNED());
					REPEAT_8(GRAYSCALE_RGB888_UNALIGNED());
					REPEAT_8(GRAYSCALE_RGB888_UNALIGNED());
				}

			}
			addr += stride;
		}
	}
	svc_flushProcessDataCache(CURRENT_PROCESS_HANDLE, (u32)addr, stride * height);
	return 0;
}


static inline __attribute__((always_inline)) u32 plgNightShift2(u32 addr, u32 stride, u32 height, u32 format, u32 isAligned) {
	if (g_plgInfo->nightShiftLevel == 1) {
        return plgNightShift3(addr, stride, height, format, 1, 0, isAligned);
	}
	if (g_plgInfo->nightShiftLevel == 2) {
        return plgNightShift3(addr, stride, height, format, 2, 0, isAligned);
	}
	if (g_plgInfo->nightShiftLevel == 3) {
        return plgNightShift3(addr, stride, height, format, 3, 0, isAligned);
	}
	if (g_plgInfo->nightShiftLevel == 4) {
        return plgNightShift3(addr, stride, height, format, 3, 1, isAligned);
	}
	if (g_plgInfo->nightShiftLevel == 5) {
        return plgNightShift3(addr, stride, height, format, 4, 1, isAligned);
	}
	if (g_plgInfo->nightShiftLevel == 6) {
		return plgScreenFilter(addr, stride, height, format, 1);
	}
	if (g_plgInfo->nightShiftLevel == 7) {
		return plgScreenFilter(addr, stride, height, format, 2);
	}
	return 0;
}

u32 plgNightShiftFramebuffer(u32 addr, u32 stride, u32 height, u32 format) {
    if ((addr % 4 == 0) && (stride % 4 == 0)) {
        return plgNightShift2(addr, stride, height, format, 1);
    }
    return plgNightShift2(addr, stride, height, format, 0);
}