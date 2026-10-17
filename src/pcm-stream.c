#include "pcm-stream.h"
#include "resample.h"
#include <string.h>
void pcm_stream_init(pcm_stream_t *s, void *context,
    int (*write)(void *, const uint8_t *, size_t))
{
    memset(s,0,sizeof *s); s->context=context; s->write=write;
    s->random=0x8a5cd789u;
}
static uint32_t random_word(pcm_stream_t *s)
{
    uint32_t x=s->random;
    x^=x<<13; x^=x>>17; x^=x<<5;
    return s->random=x;
}
static int emit(pcm_stream_t *s, int16_t b)
{
    int32_t numerator;
#if defined(__sh__)
    /* Each operand fits signed 16 bits, including weight <= 28800.
       Two products avoid overflowing the signed 16-bit sample difference. */
    numerator=cgplayer_dsp_product(s->previous,OUTPUT_RATE-s->phase)+
        cgplayer_dsp_product(b,s->phase);
#else
    numerator=(int32_t)s->previous*(int32_t)(OUTPUT_RATE-s->phase)+
        (int32_t)b*(int32_t)s->phase;
#endif
    int32_t sample=numerator/(int32_t)OUTPUT_RATE;
    /* TPDF dither at final u8 conversion, after mixing and interpolation. */
    int32_t noise=(int32_t)(random_word(s)&255)-(int32_t)(random_word(s)&255);
    int32_t value=(sample+32768+128+noise);
    if(value<0) value=0;
    value/=256;
    if(value>255) value=255;
    s->buffer[s->used++]=(uint8_t)value;
    s->output_samples++;
    if(s->used==sizeof s->buffer) {
        if(s->write(s->context,s->buffer,s->used)) return -1;
        s->used=0;
    }
    return 0;
}
static int interval(pcm_stream_t *s, int16_t b)
{
    while(s->phase<OUTPUT_RATE) {
        if(emit(s,b)) return -1;
        s->phase+=s->rate;
    }
    s->phase-=OUTPUT_RATE;
    s->previous=b;
    return 0;
}
int pcm_stream_push(pcm_stream_t *s, const int16_t *pcm, size_t n, uint32_t rate)
{
    if(rate<4000 || rate>48000 || (s->rate && s->rate!=rate)) return -1;
    s->rate=rate;
    for(size_t i=0;i<n;i++) {
        if(!s->pending) { s->previous=pcm[i]; s->pending=1; }
        else if(interval(s,pcm[i])) return -1;
        s->input_samples++;
    }
    return 0;
}
int pcm_stream_finish(pcm_stream_t *s)
{
    if(s->pending) {
        if(interval(s,s->previous)) return -1;
        s->pending=0;
    }
    if(s->used && s->write(s->context,s->buffer,s->used)) return -1;
    s->used=0;
    return 0;
}
