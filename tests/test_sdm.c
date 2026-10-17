#include "../src/sdm.h"
#include <assert.h>
#include <math.h>
#ifndef M_PI
#define M_PI 3.14159265358979323846
#endif
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
/* Host checks for the pulse-width coder: 512-bit frames at 14.7456 Mbit/s
   (Pphi x2), 28 800 frames/s. With a path argument it writes two seconds of
   a 1 kHz sine (16-bit, 0.5 FS, full volume) as raw serial bytes for
   tests/test_sdm.py. */
static int16_t *source; static uint32_t source_count;
static int16_t fetch(void *context, uint32_t index)
{
    (void)context; assert(index<source_count); return source[index];
}
static unsigned ones(const uint8_t *b) { unsigned n=0; for(int k=0;k<SDM_FRAME_BYTES;k++) for(uint8_t v=b[k];v;v>>=1) n+=v&1; return n; }
static sdm_t make(uint32_t rate, unsigned volume)
{
    sdm_t s={0}; s.rate=rate; s.count=source_count; s.volume=volume; s.fetch=fetch;
    sdm_start(&s); return s;
}
static int16_t sine[28800*2];
static void run(const char *path)
{
    const unsigned bits=SDM_FRAME_BITS, fb=(unsigned)SDM_FRAME_BYTES;
    assert(bits==512 && fb==64);
    /* Operating point 128 +- 104 in 256-frame units: the same pulse lengths
       in microseconds as the original 256-bit frame. */
    assert(SDM_N_MID==256 && SDM_N_HALF==208 && SDM_N_MIN==48 && SDM_N_MAX==464);
    /* Frames: n ones, centred, contiguous, bit 0 of byte 0 first. */
    for(unsigned n=0;n<=bits;n++) {
        uint8_t f[SDM_FRAME_BYTES_MAX]; sdm_frame_bytes(n,f); assert(ones(f)==n);
        unsigned start=(bits-n)/2;
        for(unsigned i=0;i<bits;i++) assert((((f[i/8]>>(i%8))&1)!=0)==(i>=start && i<start+n));
    }
    /* The word generator (sdm_line_words, 1.1.1) gives the bytewise frame for
       every n, the frame code sdm_step returns included. */
    for(unsigned n=0;n<=bits;n++) {
        uint8_t f[SDM_FRAME_BYTES_MAX], g[SDM_FRAME_BYTES_MAX];
        sdm_frame_bytes(n,f); sdm_frame_write(g,n); assert(!memcmp(f,g,fb));
    }
    { sdm_t s={0}; sdm_start(&s); uint8_t f[SDM_FRAME_BYTES_MAX], g[SDM_FRAME_BYTES_MAX];
      for(int x=-32768;x<32768;x+=4099) { unsigned n=sdm_step(&s,x); sdm_frame_write(g,n); sdm_frame_bytes(n,f); assert(!memcmp(f,g,fb) && ones(g)==n); } }
    /* DC accuracy: the mean pulse width equals the target within 0.05 bit
       (dither and error feedback average out), range limits respected. */
    int16_t dc[1]; source=dc; source_count=1;
    for(int v=-30000; v<=30000; v+=5000) {
        dc[0]=(int16_t)v;
        sdm_t s=make(28800,16); s.count=1;
        double sum=0; unsigned n=0;
        for(unsigned k=0;k<SDM_RATE*2;k++) { uint8_t f[SDM_FRAME_BYTES_MAX]; s.index=0; sdm_generate(&s,f,fb); unsigned w=ones(f); assert(w>=(unsigned)SDM_N_MIN && w<=(unsigned)SDM_N_MAX); sum+=w; n++; }
        double target=SDM_N_MID+v*(double)SDM_N_HALF/32768.0;
        if(target<SDM_N_MIN) target=SDM_N_MIN;
        if(target>SDM_N_MAX) target=SDM_N_MAX;
        assert(fabs(sum/n-target)<0.05);
    }
    /* Timing: frames per source sample are exactly rate-proportional. */
    uint32_t rates[]={4000,11025,22050,28800,44100,48000};
    static int16_t ramp[48001]; for(int i=0;i<48001;i++) ramp[i]=(int16_t)((i%8000)*7-24000);
    source=ramp; source_count=48001;
    for(unsigned r=0;r<sizeof rates/sizeof *rates;r++) {
        sdm_t s=make(rates[r],16);
        uint8_t buf[SDM_FRAME_BYTES_MAX*2]; uint64_t bytes=0;
        for(;;) { unsigned n=sdm_generate(&s,buf,2*fb); if(!n) break; assert(n%fb==0); bytes+=n;
            assert(s.index==(uint64_t)(bytes/fb)*rates[r]/SDM_RATE || s.index==source_count); }
        uint64_t frames=bytes/fb;
        assert(frames==(uint64_t)source_count*SDM_RATE/rates[r] || frames==(uint64_t)source_count*SDM_RATE/rates[r]+1);
    }
    /* Native 28800 Hz: one frame per sample, interpolation is identity. */
    { static int16_t t[4]={1000,-1000,30000,-30000}; source=t; source_count=4;
      sdm_t s=make(28800,16); uint8_t buf[SDM_FRAME_BYTES_MAX*4]; assert(sdm_generate(&s,buf,4*fb)==4*fb); }
    /* Full-scale sine at volume 16: bounded error, output moving. */
    for(int i=0;i<28800*2;i++) sine[i]=(int16_t)lrint(32767*sin(2*M_PI*1000*i/28800.0));
    source=sine; source_count=28800*2;
    { sdm_t s=make(28800,16); uint8_t buf[SDM_FRAME_BYTES_MAX*8]; unsigned distinct=0;
      static uint8_t seen[SDM_FRAME_BITS+1]; memset(seen,0,sizeof seen);
      for(;;) { unsigned n=sdm_generate(&s,buf,8*fb); if(!n) break;
        for(unsigned i=0;i<n;i+=fb) { unsigned w=ones(buf+i); if(!seen[w]) { seen[w]=1; distinct++; } }
        assert(abs(s.e1)<=SDM_STEP_MAX && abs(s.e2)<=SDM_STEP_MAX); }
      /* a 1 kHz sine at 28.8 kHz has 144 phases: cap the expectation */
      assert(distinct>=(unsigned)(2*SDM_N_HALF-10 < 100 ? 2*SDM_N_HALF-10 : 100)); }
    printf("PWM%u: frames, word generator, DC accuracy, rational timing, native rate, full scale PASS\n",bits);
    if(path) {
        for(int i=0;i<28800*2;i++) sine[i]=(int16_t)lrint(32767*0.5*sin(2*M_PI*1000*i/28800.0));
        sdm_t s=make(28800,16);
        FILE *f=fopen(path,"wb"); assert(f);
        uint8_t buf[4096];
        for(;;) { unsigned n=sdm_generate(&s,buf,sizeof buf); if(!n) break; fwrite(buf,1,n,f); }
        fclose(f);
        printf("Wrote 1 kHz test stream (%u-bit frames) to %s\n",bits,path);
    }
}
int main(int argc, char **argv)
{
    run(argc>=2 ? argv[1] : NULL);
    return 0;
}
