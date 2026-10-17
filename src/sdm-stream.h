#ifndef LOFI100_SDM_STREAM_H
#define LOFI100_SDM_STREAM_H
#include "sdm.h"
#include <stddef.h>
/* Streaming modulator: PCM at any rate 4-48 kHz in, 64-byte (512-bit) SDM
   frames at 28 800 frames/s out. Rates other than 28 800 Hz go through a
   windowed-sinc polyphase resampler (0.9.1; before, linear interpolation,
   whose images folded back into the band: -43 dBFS A-weighted error for
   24 kHz Opus, more than the modulator and the line together). */
#define SDM_RS_PHASES 64            /* table rows; interpolated between rows */
#define SDM_RS_MAX_TAPS 48          /* enough for 48 kHz -> 28.8 kHz */
#define SDM_RS_HIST 64              /* power of two >= SDM_RS_MAX_TAPS */
typedef struct {
    sdm_t mod;
    uint32_t phase,rate;            /* output position inside the segment */
    uint32_t pushed;                /* input samples received (saturating) */
    int taps;                       /* 0: bypass (28 800 Hz) */
    unsigned hpos;                  /* next write position in hist */
    int16_t hist[2*SDM_RS_HIST];    /* doubled ring: windows are contiguous */
    /* gain and look-ahead limiter, at every volume (1.0.2) */
    int32_t gain, gain_target;      /* Q16, volume x makeup; gain follows target */
    uint32_t makeup;                /* Q12, 4096 = none; decoders: DECODE_MAKEUP_Q12 */
    int32_t la[64];                 /* delay line, SDM_LA_FRAMES deep */
    unsigned la_pos, la_fill;       /* la_fill: frames held (< SDM_LA_FRAMES while priming) */
    int32_t la_gain;                /* Q15, 32768 = unity */
    void *context;
    int (*write)(void *,unsigned);  /* one frame per call: its code (sdm.h), see tx_feed_free() */
} sdm_stream_t;
/* The sink gets one frame code per output frame and writes the frame itself
   (tx_ring_put_frame: straight into the ring, 1.1.1); nonzero stops the
   stream. */
void sdm_stream_init(sdm_stream_t *,unsigned,void *,int (*)(void *,unsigned));
/* Volume 1..SDM_VOLUME_TOP, changeable while streaming (keys UP/DOWN).
   1..16 attenuate in sixteenths; 17..24 add 1.5 dB per step (+12 dB at 24).
   A volume change glides to the new gain over ~4 ms (no step, no click).
   Every volume goes through the same look-ahead limiter: 48 frames (1.7 ms)
   of delay, the gain is brought down before a peak arrives (0.3 ms) and
   released over ~140 ms, so loud material and resampler overshoot are
   compressed, not clipped. Below the threshold (31000) the output is the
   input times the gain, frame-aligned: the delay is primed at the start
   (the first 48 frames are held back) and drained by sdm_stream_finish. */
#define SDM_LA_FRAMES 48
void sdm_stream_set_volume(sdm_stream_t *,unsigned);
/* Gain that undoes the decoders' output headroom (mp3-decode.h), Q12;
   4096 (the default) for WAV. Set before the first push. */
void sdm_stream_set_makeup(sdm_stream_t *,uint32_t);
int sdm_stream_push(sdm_stream_t *,const int16_t *,size_t,uint32_t);
int sdm_stream_finish(sdm_stream_t *);
/* Resampler alone, for tests: the value (before volume and modulation)
   the stream would modulate for every output frame, via a callback. */
typedef void (*sdm_stream_tap_t)(void *ctx,int32_t value);
void sdm_stream_set_tap(sdm_stream_tap_t tap,void *ctx);
#endif
