#ifndef FUNC_H
#define FUNC_H

#include "3ds/types.h"

#define REG(x)   (*(volatile u32*)(x))
#define REG8(x)  (*(volatile  u8*)(x))
#define REG16(x) (*(volatile u16*)(x))

#define MAX(a, b) ((a) > (b) ? (a) : (b))
#define MIN(a, b) ((a) < (b) ? (a) : (b))
#define CLAMP(v, a, b) MAX(MIN(v, b), a)

#define ALIGN_TO_PAGE_SIZE(size) ((size) == 0 ? 0 : ((((size) - 1) / 0x1000) + 1) * 0x1000)
#define PAGE_OF_ADDR(addr) ((addr) / 0x1000 * 0x1000)

#define AFAR(p, n) __atomic_fetch_add(p, n, __ATOMIC_RELAXED)
#define ASFR(p, n) __atomic_sub_fetch(p, n, __ATOMIC_RELAXED)

#define ATSR(p) __atomic_test_and_set(p, __ATOMIC_RELAXED)
#define ACR(p) __atomic_clear(p, __ATOMIC_RELAXED)

#define ALC(p) __atomic_load_n(p, __ATOMIC_CONSUME)
#define ASL(p, n) __atomic_store_n(p, n, __ATOMIC_RELEASE)

#define ATSC(p) __atomic_test_and_set(p, __ATOMIC_CONSUME)
#define ACL(p) __atomic_clear(p, __ATOMIC_RELEASE)

void memcpy_ctr(void* dst, void* src, size_t size);

#endif
