/* Host-only stubs of the gint API, for -Werror syntax checks of the player
   sources without the SH toolchain (tests/check-syntax.sh). Not a model of
   gint's behaviour. */
#pragma once
#include <stdint.h>
typedef struct { void *f; uintptr_t a[4]; } gint_call_t;
#define GINT_CALL(f, ...) ((gint_call_t){ (void *)(f), {0} })
#define GINT_CALL_NULL ((gint_call_t){ 0, {0} })
int gint_world_switch(gint_call_t call);
void gint_setrestart(int restart);
