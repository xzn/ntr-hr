#include "main.h"

void __attribute__((weak)) c_entry(u32 *) {
	main();
}
