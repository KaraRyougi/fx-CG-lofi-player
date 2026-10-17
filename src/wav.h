#ifndef LOFI100_WAV_H
#define LOFI100_WAV_H
#include <stddef.h>
#include <stdint.h>
typedef struct {
    const uint8_t *samples;      /* raw little-endian PCM bytes */
    uint32_t count, rate, bits;  /* bits is 8 (unsigned) or 16 (signed) */
} wav_t;
typedef struct {
    uint32_t offset, count, rate, bits;
} wav_info_t;
typedef int (*wav_reader_t)(void *context, uint32_t offset, void *data, size_t size);
const char *wav_parse_reader(wav_reader_t read, void *context,
    uint32_t size, wav_info_t *out);
/* PCM mono, unsigned 8-bit or signed 16-bit, 4-48 kHz; rejects truncated
   RIFF chunks. count is in samples, not bytes. */
const char *wav_parse(const uint8_t *data, size_t size, wav_t *out);
/* CGplayer's 2014 encoding: 256 serial bits per 8-bit sample. */
void cgplayer_encode(uint8_t sample, uint8_t out[32]);
#endif
