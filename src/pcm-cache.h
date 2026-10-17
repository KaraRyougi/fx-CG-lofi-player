#ifndef LOFI100_PCM_CACHE_H
#define LOFI100_PCM_CACHE_H
#include <stddef.h>
#include <stdint.h>
#define PCM_CACHE_BLOCK 16384u
typedef struct { uint8_t **blocks; uint32_t size, count, capacity; } pcm_cache_t;
int pcm_cache_write(void *, const uint8_t *, size_t);
uint8_t pcm_cache_sample(const pcm_cache_t *, uint32_t);
void pcm_cache_free(pcm_cache_t *);
void pcm_cache_publish(pcm_cache_t *);
#endif
