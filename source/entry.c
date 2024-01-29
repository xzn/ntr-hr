#include "main.h"

extern u32 __c_bss_start;
extern u32 __c_bss_end;

void c_entry(u32 *) {
	main();
}
