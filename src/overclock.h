#ifndef LOFI100_OVERCLOCK_H
#define LOFI100_OVERCLOCK_H
/* Apply the Ptune 4 F5 profile in the gint world; OS state is restored by gint. */
void player_overclock_init(void);
/* Pphi x2 (58.9824 MHz, FRQCR P1FC 3 -> 2) on top of F5, switched on
   around every stream (512-bit frames, see sdm.h); 0 returns to the normal
   F5 profile for the UI. clock_freq()->Pphi_f follows. */
void player_overclock_pphi_x2(int enable);
int player_overclock_pphi_x2_active(void);
#endif
