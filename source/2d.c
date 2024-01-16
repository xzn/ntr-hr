#include "2d.h"
#include "font.h"
#include "memory.h"
#include "string.h"



void paint_square(int x, int y, u8 r, u8 g, u8 b, int w, int h, int screen){
	int x1, y1;

	for (x1 = x; x1 < x+w; x1++){
		for (y1 = y; y1 < y+h; y1++){
			paint_pixel(x1,y1,r,g,b,screen);
		}
	}
}

void paint_pixel(u32 x, u32 y, u8 r, u8 g, u8 b, int screen){
	if (x >= BOTTOM_WIDTH) {
		return;
	}
	if (y >= BOTTOM_HEIGHT) {
		return;
	}

	if (bottomFrameIsVid) {
		int coord = BOTTOM_VID_PITCH*x+BOTTOM_VID_PITCH-(y*BOTTOM_VID_BPP)-BOTTOM_VID_BPP;
		write_color_vid(coord+screen,r,g,b);
	} else {
		int coord = bottomFrameBufferPitch*x+BOTTOM_UI_PITCH-(y*BOTTOM_UI_BPP)-BOTTOM_UI_BPP;
		write_color(coord+screen,r,g,b);
	}
}

void blank(int x, int y, int xs, int ys){
	paint_square(x,y,255,255,255,xs,ys,BOTTOM_FRAME1);
	// paint_square(x,y,255,255,255,xs,ys,BOTTOM_FRAME2);
}

void paint_letter(char letter, int x, int y, u8 r, u8 g, u8 b, int screen) {

	int i;
	int k;
	int c;
	unsigned char mask;
	unsigned char l;
	if ((letter < 32) || (letter > 127)) {
		letter = '?';
	}
	c = (letter - 32) * 12;

	for (i = 0; i < 12; i++){
		mask = 0b10000000;
		l = font[i + c];
		for (k = 0; k < 8; k++){
			if ((mask >> k) & l){
				paint_pixel(k + x, i + y, r, g, b, screen);
			}
			else {
				paint_pixel(k + x, i + y, 255, 255, 255, screen);
			}
		}
	}
}
void paint_word(char* word, int x,int y, u8 r, u8 g, u8 b, int screen){
	int tmp_x =x;
	unsigned int i;
	int line = 0;

	for (i = 0; i <strlen(word); i++){

		if (tmp_x+8 > BOTTOM_WIDTH) {
			line++;
			tmp_x = x;
		}
		paint_letter(word[i],tmp_x,y+(line*8),r,g,b, screen);

		tmp_x = tmp_x+8;
	}

}

