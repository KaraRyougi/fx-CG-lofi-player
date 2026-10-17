#ifndef LOFI100_MP3_DECODE_H
#define LOFI100_MP3_DECODE_H
#include <stddef.h>
#include <stdint.h>
typedef struct {
    uint32_t frames, rate, channels, recoveries, pcm_hash;
    uint64_t samples, decode_us, max_frame_us;
    int cancelled, last_error;
    const char *error;
} decode_stats_t;
typedef struct {
    void *context;
    uint32_t size;
    int (*read)(void *,uint32_t,void *,size_t);
    int (*emit)(void *,const int16_t *,size_t,uint32_t);
    uint64_t (*clock_us)(void *);
    int (*progress)(void *,const decode_stats_t *);
} decode_io_t;
/* Decoder output headroom. A master that sits at 0 dBFS comes out of a lossy
   codec with peaks above full scale (+3.5 dB on build-host-mp3/song_32k.opus,
   about 6 overs a second in loud passages). Both fixed-point decoders keep
   those peaks internally and only clip when they write 16-bit PCM, so they
   write 4 dB lower and the stream (sdm-stream.c) adds the 4 dB back in front
   of its look-ahead limiter. vendor/opus/config.h holds the same value as
   OPUS_OUTPUT_HEADROOM_Q15 for the Opus patch. */
#define DECODE_HEADROOM_Q15 20675   /* 32768 * 10^(-4/20) */
#define DECODE_MAKEUP_Q12 6492      /* 4096 * 10^(4/20) */
int mp3_decode(const decode_io_t *io,decode_stats_t *stats);
/* "Decode while waiting", MP3 side (see mp3-decode.c). */
int mp3_fill_init(void);
void mp3_fill_step(void);
void mp3_fill_free(void);
void wav_header(uint8_t header[44],uint32_t samples,uint32_t rate);
#endif
