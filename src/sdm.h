#ifndef LOFI100_SDM_H
#define LOFI100_SDM_H
#include <stdint.h>
#include <string.h>
/* Pulse-width coder with a 16-bit front end: 28 800 frames/s, one centred
   high pulse of n bits per 512-bit frame, n in [SDM_N_MIN, SDM_N_MAX].

   Why PWM with long pulses (tests A-D, 2026-10-14): the CG100 serial
   driver passes pulses shorter than ~1.6 us only weakly, loses a gap-
   dependent part of every leading edge, and its gain drifts with the
   average duty. Long pulses and few transitions are what it handles best.

   Frame: 512 bits at 14.7456 Mbit/s (Pphi doubled to 58.9824 MHz around
   the stream, see overclock.c), the same 28 800 frames/s and the same
   pulse lengths in microseconds as the original 256-bit frame on the
   normal clock, but edges on a grid twice as fine: one more bit of
   resolution (261014_39: -3.4 dB floor). Since 0.9.3 this is the only
   frame length.

   Operating point 128 +- 104 in 256-frame units (256 +- 208 here): the
   full linear range of the driver; every pulse and gap is at least 3.3 us.
   The 261014_28 range sweep found the noise floor independent of the
   operating point and 128+-104 the loudest with the lowest distortion.

   Quantization: TPDF dither of +-0.5 LSB and half first-order error
   feedback (noise transfer 1 - 0.5 z^-1). The pulse is centred so that its
   centroid does not move with n (a left- or right-aligned pulse is a delay
   modulation of up to +-7 us, -27 dB H2 at 1 kHz). */
#define SDM_RATE 28800u           /* frames per second */
#define SDM_FRAME_BITS 512u
#define SDM_FRAME_BYTES 64
#define SDM_FRAME_BYTES_MAX SDM_FRAME_BYTES
#define SDM_N_MID 256
/* Operating point. 208 keeps every pulse and gap >= 48 bits (3.3 us), the
   driver's linear range (THD -48.8 dB at -6 dBFS, -41.8 at -1 dBFS, 261014_28).
   256 is the full range: +1.8 dB louder, THD -46.7 / -36.3 dB near full scale.
   The stream opens it when the volume is above unity (sdm_stream_set_volume). */
static int sdm_n_half=208;
#define SDM_N_HALF sdm_n_half
#define SDM_N_MIN (SDM_N_MID-SDM_N_HALF)
#define SDM_N_MAX (SDM_N_MID+SDM_N_HALF)
#define SDM_VOLUME_MAX 16         /* unity gain */
#define SDM_VOLUME_TOP 24         /* 17..24: +1.5 dB per step up to +12 dB; every volume goes through the look-ahead limiter (sdm-stream.c) */
#define SDM_VOLUME_SAFE 16        /* full range is linear: no headroom needed */
#define SDM_STEP_MAX 256          /* one pulse bit in 8.8 fixed point */

typedef int16_t (*sdm_fetch_t)(void *context, uint32_t index);
typedef struct {
    int32_t e1, e2;              /* quantization error history (8.8 fixed) */
    uint32_t rate;               /* source sample rate, 4000..48000 */
    uint32_t phase;              /* 0..SDM_RATE-1, position inside a sample */
    uint32_t index, count;       /* source sample index and total */
    int32_t acc, slope;          /* 16.16 interpolant and per-frame increment */
    unsigned volume;             /* 0..SDM_VOLUME_MAX, in sixteenths */
    uint32_t noise;              /* dither generator state */
    sdm_fetch_t fetch;
    void *context;
} sdm_t;

/* Byte pattern of a centred pulse of n bits, bit 0 of byte 0 sent first
   (odd n sit half a bit early). out must hold SDM_FRAME_BYTES bytes.
   Bytewise reference for the host tests; the player writes frames with
   sdm_line_words() (tx_ring_put_frame). */
static inline void sdm_frame_bytes(unsigned n, uint8_t *out)
{
    unsigned start=(SDM_FRAME_BITS-n)/2, end=start+n;
    for(unsigned k=0;k<SDM_FRAME_BITS/8;k++) {
        unsigned lo=8*k;
        uint8_t v;
        if(lo+8<=start || lo>=end) v=0;
        else {
            unsigned b0=lo<start ? start-lo : 0, b1=lo+8>end ? end-lo : 8;
            v=(uint8_t)(((0xffu<<b0)&0xffu)&(0xffu>>(8-b1)));
        }
        out[k]=v;
    }
}
/* Frames are generated, not looked up (1.1.1). A frame code (the pulse
   width n, 0..SDM_FRAME_BITS) becomes 64 bytes as two 256-bit lines, one
   32-byte cache line each, with one run of ones [s, e) per line (the
   centred pulse spans both lines).
   Until 1.1.0 frames were copied from a 513 x 64-byte table (32.8 KB of
   SDRAM, two cache lines per frame) because building one bytewise cost
   ~800 cycles (0.7.1); a word at a time it is a few instructions a word. */
static inline void sdm_line_run(unsigned frame, unsigned line, unsigned *s, unsigned *e)
{
    unsigned start=(SDM_FRAME_BITS-frame)/2, end=start+frame, lo=256*line;
    unsigned a=start>lo ? start-lo : 0, b=end>lo ? end-lo : 0;
    if(a>256) a=256;
    if(b>256) b=256;
    *s=a; *e=b>a ? b : a;
}
/* The 8 words of a 256-bit line whose bits [s, e) are ones, as they are
   stored. Words wholly inside the run are all ones, words outside are
   zero (both the same in any byte order); at most two words hold an edge.
   Bit i of the frame is bit i%8 of byte i/8 (sent first): in a little-
   endian word that is bit i%32, so an edge is a contiguous mask there; on
   the big-endian SH its bytes are swapped (swap.b/swap.w). */
static inline uint32_t sdm_edge_word(uint32_t m)
{
#if defined(__BYTE_ORDER__) && __BYTE_ORDER__==__ORDER_BIG_ENDIAN__
    m=__builtin_bswap32(m);
#endif
    return m;
}
static inline void sdm_line_words(unsigned s, unsigned e, uint32_t v[8])
{
    for(unsigned k=0;k<8;k++) v[k]=(32*k>=s && 32*k+32<=e) ? 0xffffffffu : 0;
    if(s<e) {
        unsigned ks=s>>5, ke=(e-1)>>5;
        uint32_t ms=0xffffffffu<<(s&31), me=0xffffffffu>>(31-((e-1)&31));
        if(ks==ke) v[ks]=sdm_edge_word(ms&me);
        else { v[ks]=sdm_edge_word(ms); v[ke]=sdm_edge_word(me); }
    }
}
/* The frame into any buffer (host tests, reference generator). */
static inline void sdm_frame_write(uint8_t *out, unsigned frame)
{
    for(unsigned line=0;line<2;line++) {
        unsigned s,e; sdm_line_run(frame,line,&s,&e);
        uint32_t v[8]; sdm_line_words(s,e,v);
        memcpy(out+32*line,v,32);
    }
}
static inline uint32_t sdm_random(sdm_t *s)
{
    uint32_t x=s->noise; x^=x<<13; x^=x>>17; x^=x<<5; return s->noise=x;
}
/* One frame: x is the volume-scaled 16-bit sample. Returns the pulse width
   n (the frame code; the caller writes the frame). */
static inline unsigned sdm_step(sdm_t *s, int32_t x)
{
    /* target in 8.8 fixed point: mid + x * half / 32768; (x*half)>>7 is
       (x*half*256)>>15 exactly and keeps the product within 24 bits. */
    int32_t target=SDM_N_MID*256+((x*SDM_N_HALF)>>7);
    int32_t u=target-s->e1/2;                  /* half error feedback */
    /* TPDF dither of +-0.5 LSB peak (two uniform draws), inside the loop so
       that it is shaped as well and breaks up limit cycles. */
    uint32_t r=sdm_random(s);
    int32_t dither=(int32_t)(r&127u)-(int32_t)((r>>8)&127u);
    int32_t n=(u+dither+128)>>8;
    if(n<SDM_N_MIN) n=SDM_N_MIN; else if(n>SDM_N_MAX) n=SDM_N_MAX;
    int32_t e=n*256-u;
    if(e>SDM_STEP_MAX) e=SDM_STEP_MAX; else if(e<-SDM_STEP_MAX) e=-SDM_STEP_MAX;
    s->e2=s->e1; s->e1=e;
    return (unsigned)n;
}
/* Reference generator with linear interpolation, kept for the host tests
   (tests/test_sdm.c; tests/test_live_stream.c compares the streaming
   modulator against it at 28 800 Hz, where both are one frame per sample).
   The player itself uses sdm-stream.c. */
static inline int32_t sdm_scaled(const sdm_t *s, uint32_t index)
{
    int32_t v = s->fetch(s->context, index);
    return (v * (int32_t)s->volume) / SDM_VOLUME_MAX;
}
static inline void sdm_segment(sdm_t *s)
{
    int32_t a = sdm_scaled(s, s->index);
    if(s->rate == SDM_RATE) { s->slope = 0; s->acc = a * 65536; return; }
    int32_t b = s->index + 1 < s->count ? sdm_scaled(s, s->index + 1) : a;
    int32_t d = b - a;
    const uint32_t recip = 4294967296ull / SDM_RATE;
    int64_t slope = ((int64_t)d * (int64_t)s->rate * (int64_t)recip) >> 16;
    if(slope > INT32_MAX) slope = INT32_MAX; else if(slope < INT32_MIN) slope = INT32_MIN;
    s->slope = (int32_t)slope;
    s->acc = (int32_t)((int64_t)a * 65536 + ((((int64_t)d * (int64_t)s->phase * (int64_t)recip) >> 16)));
}
static inline void sdm_start(sdm_t *s)
{
    s->e1 = 0; s->e2 = 0;
    if(!s->noise) s->noise = 0x8a5cd789u;
    if(s->index < s->count) sdm_segment(s);
}
/* Fill up to n bytes (a multiple of SDM_FRAME_BYTES); returns the number
   produced, 0 at end of stream. */
static inline unsigned sdm_generate(sdm_t *s, uint8_t *out, unsigned n)
{
    unsigned produced = 0;
    while(produced + SDM_FRAME_BYTES <= n && s->index < s->count) {
        sdm_frame_write(out + produced, sdm_step(s, s->acc >> 16));
        produced += SDM_FRAME_BYTES;
        s->acc = (int32_t)((uint32_t)s->acc + (uint32_t)s->slope);
        s->phase += s->rate;
        if(s->phase >= SDM_RATE) {
            do { s->phase -= SDM_RATE; s->index++; } while(s->phase >= SDM_RATE);
            if(s->index < s->count) sdm_segment(s);
        }
    }
    return produced;
}
#endif
