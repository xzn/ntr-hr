#ifndef FUNC_H
#define FUNC_H

#include <stdarg.h>
#include "3ds/os.h"

#define REG(x)   (*(volatile u32*)(x))
#define REG8(x)  (*(volatile  u8*)(x))
#define REG16(x) (*(volatile u16*)(x))

#define MAX(a, b) ((a) > (b) ? (a) : (b))
#define MIN(a, b) ((a) < (b) ? (a) : (b))

#endif
