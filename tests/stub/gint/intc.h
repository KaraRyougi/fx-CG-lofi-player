#pragma once
#include <gint/gint.h>
void *intc_handler_function(int event_code, gint_call_t function);
int intc_priority(int intname, int level);
enum { INTC_DMA_DEI5=42 };
