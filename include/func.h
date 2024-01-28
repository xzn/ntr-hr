#ifndef FUNC_H
#define FUNC_H

#include "3ds/types.h"

#define REG(x)   (*(volatile u32*)(x))
#define REG8(x)  (*(volatile  u8*)(x))
#define REG16(x) (*(volatile u16*)(x))

#define MAX(a, b) ((a) > (b) ? (a) : (b))
#define MIN(a, b) ((a) < (b) ? (a) : (b))
#define CLAMP(v, a, b) MAX(MIN(v, b), a)

#define AFAR(v, n) __atomic_fetch_add(&v, n, __ATOMIC_RELAXED)
#define ASFR(v, n) __atomic_sub_fetch(&v, n, __ATOMIC_RELAXED)

#define ATSR(v) __atomic_test_and_set(&v, __ATOMIC_RELAXED)
#define ACR(v) __atomic_clear(&v, __ATOMIC_RELAXED)
#define ALR(v) __atomic_load_n(&v, __ATOMIC_RELAXED)

#define ATSL(p) __atomic_test_and_set(p, __ATOMIC_CONSUME)
#define ACL(p) __atomic_clear(p, __ATOMIC_RELEASE)

void memcpy_ctr(void* dst, void* src, size_t size);

#endif
