#ifndef MEMORY_H
#define MEMORY_H
#include "math.h"
#include "3dstypes.h"


void write_byte(u32 address, u8 byte);
void write_word(u32 address, u32 word);
void write_color(u32 address, u8 r, u8 g, u8 b);
u32 read_word(u32 address);
char nibble_to_readable(u8 nibble);
void byte_to_string(u8 byte, char* ret, int max_len);
u32 byte_to_bit_string(u8 byte, char* ret, int max_len);
void u32_to_string(u32 byte, char* ret, int max_len);
u32 u16_to_string(u16 sh, char* ret, int max_len);
u32 u16_to_bit_string(u16 sh, char* ret, int max_len);

#endif

