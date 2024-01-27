#ifndef FUNC_H
#define FUNC_H

#include "3ds/types.h"

#define REG(x)   (*(volatile u32*)(x))
#define REG8(x)  (*(volatile  u8*)(x))
#define REG16(x) (*(volatile u16*)(x))

#define MAX(a, b) ((a) > (b) ? (a) : (b))
#define MIN(a, b) ((a) < (b) ? (a) : (b))
#define CLAMP(v, a, b) MAX(MIN(v, b), a)

#define ATSR(v) __atomic_test_and_set(&v, __ATOMIC_RELAXED)
#define ACR(v) __atomic_clear(&v, __ATOMIC_RELAXED)

#define ATSL(p) __atomic_test_and_set(p, __ATOMIC_CONSUME)
#define ACL(p) __atomic_clear(p, __ATOMIC_RELEASE)

#endif
