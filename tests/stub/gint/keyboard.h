#pragma once
enum { KEYEV_NONE, KEYEV_DOWN, KEYEV_UP, KEYEV_HOLD };
enum { KEY_BACK=1, KEY_TOOLS, KEY_UP, KEY_DOWN, KEY_PAGEUP, KEY_PAGEDOWN, KEY_EXE, KEY_OK, KEY_LEFT, KEY_RIGHT,
  KEY_0, KEY_1, KEY_2, KEY_3, KEY_4, KEY_5, KEY_6, KEY_7, KEY_8, KEY_9, KEY_DOT, KEY_EXP, KEY_NEG, KEY_ADD, KEY_SUB, KEY_MUL, KEY_DIV, KEY_SQUARE, KEY_EXIT, KEY_AC, KEY_HOME, KEY_ACON };
enum { GETKEY_POWEROFF=0x0400, GETKEY_DEFAULT=0x05df };
typedef struct { int type, key, shift, alpha; } key_event_t;
key_event_t getkey(void);
key_event_t getkey_opt(int options, volatile int *timeout);
key_event_t pollevent(void);
void clearevents(void);
int keydown(int key);
