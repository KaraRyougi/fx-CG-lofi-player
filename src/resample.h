#ifndef LOFI100_RESAMPLE_H
#define LOFI100_RESAMPLE_H
#include <stdint.h>
#define OUTPUT_RATE 28800u
#if defined(__sh__)
/* Signed 16-bit operands; PMULS fractional scaling is undone in assembly. */
int cgplayer_dsp_product(int difference, unsigned phase);
#endif
static inline uint8_t resample_linear(uint8_t a, uint8_t b, unsigned phase)
{
    if(!phase || a==b) return a;
    int difference=(int)b-(int)a;
#if defined(__sh__)
    int product=cgplayer_dsp_product(difference,phase);
#else
    int product=difference*(int)phase;
#endif
    /* phase < 28800, so numerator stays positive and fits in 24 bits.
       Round the complete interpolant, including descending segments. */
    uint32_t numerator=(uint32_t)((int)a*28800+product+14400);
    return (uint8_t)(numerator/28800u);
}
#endif
