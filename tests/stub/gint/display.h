#pragma once
#include <stdint.h>
#define DWIDTH 396
#define DHEIGHT 224
#define C_WHITE 0xffff
#define C_BLACK 0x0000
#define C_NONE (-1)
#define C_RGB(r,g,b) ((uint16_t)(((r)<<11)|((g)<<6)|(b)))
enum { DTEXT_LEFT=0, DTEXT_CENTER=1, DTEXT_RIGHT=2 };
enum { DTEXT_TOP=0, DTEXT_MIDDLE=1, DTEXT_BOTTOM=2 };
void dclear(int color);
void dtext(int x,int y,int fg,const char *text);
void dtext_opt(int x,int y,int fg,int bg,int halign,int valign,const char *text,int size);
void dprint(int x,int y,int fg,const char *fmt,...) __attribute__((format(printf,4,5)));
void dprint_opt(int x,int y,int fg,int bg,int halign,int valign,const char *fmt,...) __attribute__((format(printf,7,8)));
void drect(int x1,int y1,int x2,int y2,int color);
void dupdate(void);
