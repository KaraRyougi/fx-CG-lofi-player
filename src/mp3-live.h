#ifndef LOFI100_MP3_LIVE_H
#define LOFI100_MP3_LIVE_H
#include <stddef.h>
#include <stdint.h>
/* Streaming playback in the gint world: decoder or WAV source -> 512-bit
   PWM modulator -> 64 KiB ring -> DMA -> SCIF. The caller must have the
   peripheral clock doubled (player_overclock_pphi_x2(1)). Return 1 when
   the user stopped (BACK), 0 when finished, -1 on error (*error says why). */
typedef int (*mp3_source_read_t)(void *,uint32_t,void *,size_t);
/* Source must be directly readable, without OS calls. */
int mp3_play_stream(mp3_source_read_t,void *,uint32_t,const char *,unsigned,const char **);
/* Ogg Opus (mono/stereo), decoded at 24 kHz. */
int opus_play_stream(mp3_source_read_t,void *,uint32_t,const char *,unsigned,const char **);
/* WAV: fetch returns the signed 16-bit sample at an index (no OS calls).
   *position is the start sample on entry and the stop position on return. */
typedef int16_t (*pcm_fetch_t)(void *context, uint32_t index);
int wav_play_stream(pcm_fetch_t fetch,void *context,uint32_t count,uint32_t rate,
    uint32_t *position,const char *title,unsigned volume,const char **error);
/* 0.9.4 option, default 1: while the producer waits for ring space the CPU
   keeps running the decoder (Opus: the last packet again; MP3: the
   synthesis filterbank of the last frame) instead of cached multiply-adds.
   Removes the 50 Hz decode-cadence hum (-15..-18 dB) at +2 dB broadband
   hiss, a supply hump at 9-11 kHz instead of 17 kHz, and a fully busy CPU
   (battery). + toggles it at any time, also while playing. */
extern int live_decode_fill;
/* Volume shared with main.c: set before a stream, read back after; UP/DOWN
   change it while playing or paused. */
extern unsigned live_volume;
/* "12/16" or "16/16 +4.5 dB" (at least 20 bytes). */
void live_volume_label(char *out,unsigned volume);
#define LIVE_WAIT_MAC 0        /* cached multiply-adds: least hiss in silence, hump ~15-17 kHz, 50 Hz hum -72 */
#define LIVE_WAIT_DECODE 1     /* keep decoding: no hum (-81), +3 dB(A) hiss, hump ~10 kHz, CPU busy */
#define LIVE_WAIT_RANDOM 2     /* random intensity: hump smeared (+6 dB instead of +15..20), hum -77, +1.3 dB(A) */
static inline const char *live_wait_name(int m) { return m==LIVE_WAIT_DECODE ? "decode" : m==LIVE_WAIT_RANDOM ? "random" : "MAC"; }
#endif
