#ifndef MAIN_H
#define MAIN_H

#include "3ds/types.h"
#include "ntr_config.h"

int main(void);

extern NTR_CONFIG *ntrConfig;

typedef void (*showDbgFunc_t)(char *);
extern showDbgFunc_t showDbgFunc;

#endif
