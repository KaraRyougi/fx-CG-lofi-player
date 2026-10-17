#include "../src/resample.h"
#include <assert.h>
#include <stdio.h>
int main(void)
{
    /* All slopes and phases, covering both rounding directions and rails. */
    for(int d=-255;d<=255;d++) {
        int a=d<0 ? -d : 0, b=a+d;
        for(unsigned phase=0;phase<OUTPUT_RATE;phase++) {
            unsigned expected=((unsigned)a*(OUTPUT_RATE-phase)+
                (unsigned)b*phase+OUTPUT_RATE/2)/OUTPUT_RATE;
            unsigned actual=resample_linear((uint8_t)a,(uint8_t)b,phase);
            assert(actual==expected);
        }
    }
    for(unsigned a=0;a<256;a++)
        for(unsigned b=0;b<256;b++)
            assert(resample_linear(a,b,0)==a);
    unsigned rates[]={4000,11025,22050,28800,44100,48000};
    for(unsigned r=0;r<sizeof rates/sizeof *rates;r++) {
        unsigned phase=0,index=0;
        for(unsigned frame=0;frame<OUTPUT_RATE*10;frame++) {
            assert(index==(uint64_t)frame*rates[r]/OUTPUT_RATE);
            assert(phase==(uint64_t)frame*rates[r]%OUTPUT_RATE);
            phase+=rates[r];
            while(phase>=OUTPUT_RATE) { phase-=OUTPUT_RATE; index++; }
        }
        assert(index==rates[r]*10 && phase==0);
    }
    puts("Linear interpolation: all slopes/phases, native identity, rational duration PASS");
}
