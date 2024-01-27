#include "global.h"

int main(void) {
	startupInit();

	// TODO
	nsStartup();

	return 0;
}

void nsHandlePacket(void) {
	nsHandleDbgPrintPacket();
}
