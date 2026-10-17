#pragma once
#include <stdint.h>
#include <gint/gint.h>
enum { TIMER_CONTINUE=0, TIMER_STOP=1 };
#define TIMER_ANY (-1)
#define TIMER_TMU (-2)
#define TIMER_ETMU (-3)
#define TIMER_Pphi_4 0x00
#define TIMER_Pphi_16 0x10
#define TIMER_Pphi_64 0x20
#define TIMER_Pphi_256 0x30
int timer_configure(int timer, uint64_t delay, gint_call_t call);
void timer_start(int timer);
void timer_pause(int timer);
void timer_stop(int timer);
