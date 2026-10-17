#ifndef LOFI100_OPUS_FILE_H
#define LOFI100_OPUS_FILE_H
#include "mp3-decode.h"   /* decode_io_t, decode_stats_t */
#include <stdint.h>
/* Ogg Opus (RFC 7845, channel mapping family 0: mono or stereo) decoded with
   fixed-point libopus straight to 24 kHz mono (the decoder downmixes; 24 kHz
   keeps the 12 kHz the line can carry and halves the work of 48 kHz).
   start_ms > 0 seeks by bisection on granule positions, decodes at least
   80 ms of pre-roll and discards it, then emits from the exact sample.
   Returns 0 at the end, 1 when io->progress cancelled, -1 on error
   (stats->error says why). stats->samples counts emitted samples. */
#define OPUS_FILE_RATE 24000u
int opus_file_decode(const decode_io_t *io, decode_stats_t *stats, uint32_t start_ms);
/* Total length in milliseconds from the last page's granule position
   (0 if unknown). Reads only the file head and tail. */
uint32_t opus_file_length_ms(const decode_io_t *io);
/* "Decode while waiting": a second decoder re-decodes the last packet as
   the wait load. init before opus_file_decode (returns -1 without RAM:
   then the normal wait load is used), step from the wait loop, free after. */
int opus_fill_init(void);
void opus_fill_step(void);
void opus_fill_free(void);
#endif
