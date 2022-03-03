#include "memory.h"


/*void write_word(u32 address, u32 word){
  u32 *a = (u32*) address;
  *a = word;
}*/

void write_color(u32 address, u8 r, u8 g, u8 b){
  write_byte(address, b);
  write_byte(address+1, g);
  write_byte(address+2, r);
}

void write_byte(u32 address, u8 byte){
  char *a = (char*) address;
  *a = byte;
}

/*u8 read_byte(u32 address){
  return *(u8*)address;
}

u32 read_word(u32 address){
  return *(u32*)address;
}*/

char nibble_to_readable(u8 nibble){
  if (nibble < 10) return nibble+48;
  return nibble+55;
}

/*u32 u16_to_string(u16 sh, char* ret, int max_len){
  if (max_len < 5) return;
  byte_to_string((sh & 0xFF00) >> 8, ret, max_len);
  byte_to_string((sh & 0x00FF), ret+2, max_len);
  return 0;
}

u32 u16_to_bit_string(u16 sh, char* ret, int max_len){
  if (max_len < 16) return;
  byte_to_bit_string((sh & 0xFF00) >> 8, ret, max_len);
  byte_to_bit_string((sh & 0x00FF), ret+8, max_len);
  return 0;
}

u32 byte_to_bit_string(u8 byte, char* ret, int max_len){
  u8 mask = 0b10000000;
  if (max_len < 9) return;
  int i = 0;

  for (i = 0; i < 8; i++){
    if (byte&mask) {
      ret[i] = '1';
    } else {
      ret[i] = '0';
    }
    mask = mask >> 1;
  }
    
  ret[8] = 0;
  return 0;
}*/

void byte_to_string(u8 byte, char* ret, int max_len){
  if (max_len < 3) return;
  u32 mask = 0x0F;
  u32 i;

  for (i = 0; i < 2; i++){
    ret[1-i] = nibble_to_readable((byte&mask) >> i*4);
    mask = mask << 4;   
  }
  
  ret[2] = 0x00;
  return;
}

void u32_to_string(u32 byte, char* ret, int max_len){
  if (max_len < 9) return;
  u32 mask = 0x0000000F;
  u32 i;

  for (i = 0; i < 8; i++){
    ret[7-i] = nibble_to_readable((byte&mask) >> i*4);
    mask = mask << 4;
  }
  
  ret[8] = 0x00;
  return;
}

/*int div(int x, int y){
  int i = 0;
  int y_b = y;

  while (1){
    if (y > x){
      return i;
    } 
    y = y+y_b;
    i++;   
  }

}

int mod(int x, int y){
   return x - y*div(x,y);
}

int strlen(char* string){
  int i;
  for (i = 0; ; i++){
      if (string[i] == 0x00){
        return i;
      }
  }
}*/