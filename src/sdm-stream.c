#include "sdm-stream.h"
#include <string.h>
#include <stdint.h>

/* ---- resampler kernel -------------------------------------------------
   Windowed sinc (Kaiser, beta 8: about -80 dB side lobes), T taps (32 for
   up-sampling, 24 x rate/28800 for down-sampling),
   SDM_RS_PHASES+1 rows for fractional positions 0, 1/P, ..., 1; output
   values interpolate linearly between the two nearest rows. Cut-off at
   0.47 of the lower of the two Nyquist rates: for up-sampling (24 kHz
   Opus) it keeps 11.3 kHz and rejects the images; for down-sampling
   (44.1/48 kHz MP3) it is the anti-alias filter at 13.5 kHz. Built once per
   input rate without libm (own sine, square root and Bessel I0). */
static int16_t rs_table[(SDM_RS_PHASES+1)*SDM_RS_MAX_TAPS];
static uint32_t rs_rate;
static int rs_taps;

static double rs_sin(double x)
{
    const double pi=3.14159265358979323846;
    x-=2*pi*(double)(long long)(x/(2*pi));
    if(x>pi) x-=2*pi; else if(x<-pi) x+=2*pi;
    if(x>pi/2) x=pi-x; else if(x<-pi/2) x=-pi-x;
    double x2=x*x, term=x, sum=x;
    for(int k=1;k<10;k++) { term*=-x2/((2*k)*(2*k+1)); sum+=term; }
    return sum;
}
static double rs_sqrt(double v)
{
    if(v<=0) return 0;
    double r=v>1 ? v : 1;
    for(int i=0;i<60;i++) r=0.5*(r+v/r);
    return r;
}
static double rs_i0(double x)
{
    double sum=1, term=1, q=x*x/4;
    for(int k=1;k<40;k++) { term*=q/((double)k*k); sum+=term; if(term<1e-12*sum) break; }
    return sum;
}
static void rs_build(uint32_t rate)
{
    const double pi=3.14159265358979323846, beta=8.0;
    double ratio=(double)rate/SDM_RATE;
    int taps=32;                                  /* up-sampling: flat to ~0.42 fs */
    double fcn;                                   /* cut-off, cycles per input sample */
    if(rate<SDM_RATE) fcn=0.47;
    else {
        fcn=0.47/ratio;
        taps=(int)(24.0*ratio+0.999);
        if(taps&1) taps++;
        if(taps>SDM_RS_MAX_TAPS) taps=SDM_RS_MAX_TAPS;
    }
    int half=taps/2;
    double i0b=rs_i0(beta);
    for(int r=0;r<=SDM_RS_PHASES;r++) {
        double f=(double)r/SDM_RS_PHASES, w[SDM_RS_MAX_TAPS], sum=0;
        for(int j=0;j<taps;j++) {
            double t=f+(half-1)-j;                /* distance from the output point */
            double a=2*fcn*t, s=(a==0) ? 1.0 : rs_sin(pi*a)/(pi*a);
            double u=t/(half+0.0);
            double win=(u<=-1 || u>=1) ? 0 : rs_i0(beta*rs_sqrt(1-u*u))/i0b;
            w[j]=2*fcn*s*win; sum+=w[j];
        }
        for(int j=0;j<taps;j++) {
            double c=w[j]/sum*16384.0;            /* unity DC gain, Q14 */
            rs_table[r*SDM_RS_MAX_TAPS+j]=(int16_t)(c<0 ? c-0.5 : c+0.5);
        }
    }
    rs_rate=rate; rs_taps=taps;
}

/* ---- stream ------------------------------------------------------------ */
static sdm_stream_tap_t tap_fn;
static void *tap_ctx;
void sdm_stream_set_tap(sdm_stream_tap_t tap,void *ctx) { tap_fn=tap; tap_ctx=ctx; }

/* Gain above unity in Q8: 256 * 2^((v-16)/4) for v = 17..24. */
static const uint16_t gain_q8[SDM_VOLUME_TOP-SDM_VOLUME_MAX+1]={256,304,362,431,512,609,724,861,1024};
static int32_t target_gain(const sdm_stream_t *s)
{
    unsigned v=s->mod.volume;
    int64_t g=v<=SDM_VOLUME_MAX ? (int64_t)v*(65536/SDM_VOLUME_MAX)
                                : (int64_t)gain_q8[v-SDM_VOLUME_MAX]*256;
    return (int32_t)((g*s->makeup)>>12);
}
void sdm_stream_set_volume(sdm_stream_t *s,unsigned volume)
{
    s->mod.volume=volume>SDM_VOLUME_TOP ? SDM_VOLUME_TOP : volume ? volume : 1u;
    /* The pulse range stays at +-208: opening it to +-256 put the limited
       peaks into the driver's dead zones (audible at +3 dB). */
    sdm_n_half=208;
    if(!s->la_gain) s->la_gain=32768;
    s->gain_target=target_gain(s);
}
void sdm_stream_set_makeup(sdm_stream_t *s,uint32_t makeup)
{
    s->makeup=makeup;
    s->gain=s->gain_target=target_gain(s);
}
void sdm_stream_init(sdm_stream_t *s,unsigned volume,void *ctx,
    int (*write)(void *,unsigned))
{
    memset(s,0,sizeof *s);
    s->makeup=4096;
    sdm_stream_set_volume(s,volume);
    s->gain=s->gain_target;                  /* start at the gain, no glide */
    s->mod.noise=0x8a5cd789u;
    s->context=ctx; s->write=write;
}
/* x * gain (Q16), truncated toward zero like the old y*v/16. */
static int32_t apply_gain(int32_t x,int32_t gain)
{
    int64_t p=(int64_t)x*gain;
    return (int32_t)(p>=0 ? p>>16 : -((-p)>>16));
}
static int modulate(sdm_stream_t *s,int32_t y)
{
    if(y>32767) y=32767; else if(y<-32768) y=-32768;
    unsigned frame=sdm_step(&s->mod,y);
    return s->write(s->context,frame) ? -1 : 0;
}
/* Look-ahead limiter. y enters the delay line; the gain needed for the
   loudest sample inside the line is applied with a fast attack (reaches it
   before the peak comes out) and a slow release, to the sample leaving the
   line SDM_LA_FRAMES later. Threshold 31000 of 32767. Returns 0 without
   output while the line is still priming. */
static int limit(sdm_stream_t *s,int32_t y,int32_t *out)
{
    int32_t old=s->la[s->la_pos];
    s->la[s->la_pos]=y; s->la_pos=(s->la_pos+1)%SDM_LA_FRAMES;
    int32_t peak=0;
    for(unsigned i=0;i<SDM_LA_FRAMES;i++) { int32_t a=s->la[i]<0 ? -s->la[i] : s->la[i]; if(a>peak) peak=a; }
    int32_t want=peak>31000 ? (int32_t)((31000LL<<15)/peak) : 32768;
    if(want<s->la_gain) s->la_gain-=(s->la_gain-want+7)>>3;           /* attack: ~8 frames */
    else s->la_gain+=(want-s->la_gain+4095)>>12;                        /* release: ~4096 frames, 140 ms */
    if(s->la_fill<SDM_LA_FRAMES) { s->la_fill++; return 0; }
    *out=s->la_gain==32768 ? old : (int32_t)(((int64_t)old*s->la_gain)>>15);
    return 1;
}
static int emit_frame(sdm_stream_t *s,int32_t y)
{
    if(tap_fn) tap_fn(tap_ctx,y);
    /* glide to a new volume over ~128 frames (4.4 ms) instead of a step */
    int32_t d=s->gain_target-s->gain;
    if(d) s->gain+=(d>-128 && d<128) ? d : d/128;
    int32_t out;
    if(!limit(s,apply_gain(y,s->gain),&out)) return 0;
    return modulate(s,out);
}
static int32_t dot(const int16_t *x,const int16_t *c,int n)
{
    int32_t acc=0;
    for(int j=0;j<n;j++) acc+=(int32_t)x[j]*c[j];
    return acc;
}
static int push_one(sdm_stream_t *s,int16_t x)
{
    if(!s->taps) return emit_frame(s,x);          /* 28 800 Hz: one frame per sample */
    s->hist[s->hpos]=x; s->hist[s->hpos+SDM_RS_HIST]=x;
    s->hpos=(s->hpos+1)&(SDM_RS_HIST-1);
    if(s->pushed!=UINT32_MAX) s->pushed++;
    int taps=s->taps, half=taps/2;
    if(s->pushed<=(uint32_t)half) return 0;       /* first segment needs T/2 look-ahead */
    /* segment b = newest - T/2; its window is the last T samples */
    const int16_t *win=s->hist+((s->hpos-(unsigned)taps)&(SDM_RS_HIST-1));
    uint32_t phase=s->phase, rate=s->rate;
    while(phase<SDM_RATE) {
        uint32_t pos=phase*SDM_RS_PHASES, row=pos/SDM_RATE;
        uint32_t frac=(pos-row*SDM_RATE)*65536u/SDM_RATE;          /* Q16 */
        const int16_t *c0=rs_table+row*SDM_RS_MAX_TAPS;
        int32_t y0=dot(win,c0,taps), y1=dot(win,c0+SDM_RS_MAX_TAPS,taps);
        int64_t y=((int64_t)y0*65536+(int64_t)(y1-y0)*frac)>>30;  /* Q14+Q16 */
        if(emit_frame(s,(int32_t)y)) { s->phase=phase; return -1; }
        phase+=rate;
    }
    s->phase=phase-SDM_RATE;
    return 0;
}
int sdm_stream_push(sdm_stream_t *s,const int16_t *pcm,size_t n,uint32_t rate)
{
    if(rate<4000 || rate>48000 || (s->rate && s->rate!=rate)) return -1;
    if(!s->rate) {
        s->rate=rate;
        if(rate==SDM_RATE) s->taps=0;
        else { if(rs_rate!=rate) rs_build(rate); s->taps=rs_taps; }
    }
    for(size_t i=0;i<n;i++) if(push_one(s,pcm[i])) return -1;
    return 0;
}
int sdm_stream_finish(sdm_stream_t *s)
{
    /* Flush the look-ahead: T/2 zeros complete the segments up to the last
       real sample, so the frame count stays ceil(samples * 28800 / rate). */
    if(s->taps && s->pushed) {
        for(int i=0;i<s->taps/2;i++) if(push_one(s,0)) return -1;
        s->pushed=0;
    }
    /* Drain the limiter's delay line with one line of silence: the pushes
       that would still be priming (stream shorter than the line) give no
       output, the rest give the held frames in order. */
    if(s->la_fill) {
        for(unsigned i=0;i<SDM_LA_FRAMES;i++) {
            int32_t out;
            if(limit(s,0,&out) && modulate(s,out)) return -1;
        }
        s->la_fill=0;
    }
    return 0;
}
