#ifndef LOFI100_PCM_STREAM_H
#define LOFI100_PCM_STREAM_H
#include <stddef.h>
#include <stdint.h>
typedef struct {
    uint32_t rate, phase, random;
    uint64_t input_samples, output_samples;
    int16_t previous;
    int pending;
    uint8_t buffer[4096];
    size_t used;
    void *context;
    int (*write)(void *, const uint8_t *, size_t);
} pcm_stream_t;
void pcm_stream_init(pcm_stream_t *, void *, int (*)(void *, const uint8_t *, size_t));
int pcm_stream_push(pcm_stream_t *, const int16_t *, size_t, uint32_t);
int pcm_stream_finish(pcm_stream_t *);
#endif
