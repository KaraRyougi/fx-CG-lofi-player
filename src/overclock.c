/* Clock profile adapted from gpsp-sh4-jit/ports/fxcg100/gint-gpsp/src/overclock.c. */
#include "overclock.h"

#include <gint/clock.h>

/* Ptune 4 F5 for fx-CG50/fx-CG100 (CalcLoverHK/Sentaro21).
 *
 *   PLL 32x, CPU 1/2: 235.93 MHz
 *   SuperHyway 1/4:   117.96 MHz
 *   SDRAM/bus 1/4:    117.96 MHz, CL2, TRC6
 *   Peripheral 1/16:   29.49 MHz
 *
 * This is intentionally explicit instead of clock_set_speed(F5): the gint
 * bundled with the current fx-CG100 toolchain aliases F5 to its older F4
 * profile and therefore leaves the SDRAM bus at 58.98 MHz.
 */
static struct cpg_overclock_setting const ptune4_f5 = {
	.FLLFRQ  = 0x00004384,
	.FRQCR   = 0x1f001103,
	.CS0BCR  = 0x44900400,
	.CS2BCR  = 0x00000000,
	.CS3BCR  = 0x04904400,
	.CS5aBCR = 0x17df0400,
	.CS0WCR  = 0x000004c0,
	.CS2WCR  = 0x00000000,
	.CS3WCR  = 0x000024d2,
	.CS5aWCR = 0x000203c1,
};

/* The same profile with the peripheral clock doubled, used for playback.
 * FRQCR[3:0] is P1FC; gint decodes Pphi_div = 1 << (P1FC + 1), so 3 -> 2
 * takes Pphi from PLL/16 = 29.4912 MHz to PLL/8 = 58.9824 MHz. Everything
 * else (CPU, SuperHyway, bus, BSC wait states) is unchanged. The SCIF bit
 * clock (Pphi/4 with CKS=0, BRR=0) becomes 14.7456 Mbit/s, so a 512-bit
 * PWM frame keeps the 28 800 frames/s of the 256-bit frame on the normal
 * clock while the edge grid is twice as fine (261014_39: -3.4 dB floor).
 * The SH7305's Pphi limit is not public; the SH7724 (same core family)
 * specifies 33 MHz. KEYSC, TMU, I2C, SCIF all run from Pphi. */
static struct cpg_overclock_setting const ptune4_f5_pphi_x2 = {
	.FLLFRQ  = 0x00004384,
	.FRQCR   = 0x1f001102,
	.CS0BCR  = 0x44900400,
	.CS2BCR  = 0x00000000,
	.CS3BCR  = 0x04904400,
	.CS5aBCR = 0x17df0400,
	.CS0WCR  = 0x000004c0,
	.CS2WCR  = 0x00000000,
	.CS3WCR  = 0x000024d2,
	.CS5aWCR = 0x000203c1,
};

static int pphi_x2_active;

void player_overclock_init(void)
{
	/* Keep the overclock local to the add-in. gint restores the launcher's
	 * original CPG/BSC state for OS world switches and when main() returns,
	 * then reapplies F5 after each world switch back into the player. */
	cpg_set_overclock_setting(&ptune4_f5);
	pphi_x2_active = 0;
}

void player_overclock_pphi_x2(int enable)
{
	enable = !!enable;
	if(enable == pphi_x2_active) return;
	/* gint's cpg_set_overclock_setting() waits for asynchronous work,
	 * disables interrupts, writes FRQCR, recomputes clock_freq() and
	 * rescales the running timers' TCNT/TCOR to the new Pphi. The world
	 * switch into the OS for Serial_Open then restores the OS clocks and,
	 * on the way back, whatever this function last applied. */
	cpg_set_overclock_setting(enable ? &ptune4_f5_pphi_x2 : &ptune4_f5);
	pphi_x2_active = enable;
}

int player_overclock_pphi_x2_active(void)
{
	return pphi_x2_active;
}
