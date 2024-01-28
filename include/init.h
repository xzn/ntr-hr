#ifndef INIT_H
#define INIT_H

#include "ntr_config.h"
void startupInit(void);
void loadParams(NTR_CONFIG *ntrCfg);
void initSharedFunc(void);
int plgLoaderInfoAlloc(void);

#endif
