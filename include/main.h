#ifndef MAIN_H
#define MAIN_H

#include "3dstypes.h"
#include "../../BootNTR/source/ntr_config.h"

void createpad(void *counter, void *keyY, void *filename, u32 megabytes, u8 padnum);
int main();
void disp(u32 t, u32 cl) ;

extern u32 IoBasePad;
extern u32 IoBaseLcd;
extern u32 IoBasePdc;

extern u32 ShowDbgFunc;

extern u32 KProcessCodesetOffset;
extern u32 KProcessPIDOffset ;
extern u32 KProcessHandleDataOffset;
extern u32 HomeAptStartAppletAddr;


extern NTR_CONFIG* ntrConfig;
 
#endif

