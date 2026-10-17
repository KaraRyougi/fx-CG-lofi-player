#pragma once
#include <stdint.h>
typedef struct { int FLL, PLL, Iphi_div, Bphi_div, Pphi_div; int CKIO_f, FLL_f, PLL_f, Iphi_f, Bphi_f, Pphi_f; } clock_frequency_t;
const clock_frequency_t *clock_freq(void);
void sleep_us(uint64_t us);
void sleep_ms(uint64_t ms);
struct cpg_overclock_setting { uint32_t FLLFRQ, FRQCR, CS0BCR, CS0WCR, CS2BCR, CS2WCR, CS3BCR, CS3WCR, CS5aBCR, CS5aWCR; };
void cpg_set_overclock_setting(const struct cpg_overclock_setting *s);
void cpg_get_overclock_setting(struct cpg_overclock_setting *s);
void cpg_compute_freq(void);
