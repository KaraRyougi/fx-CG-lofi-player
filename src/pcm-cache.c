#include "pcm-cache.h"
#include <stdlib.h>
#include <string.h>
int pcm_cache_write(void *context, const uint8_t *data, size_t n)
{
    pcm_cache_t *c=context;
    if(n>UINT32_MAX-c->size) return -1;
    while(n) {
        unsigned offset=c->size%PCM_CACHE_BLOCK;
        if(!offset) {
            if(c->count==c->capacity) {
                uint32_t capacity=c->capacity ? c->capacity*2 : 16;
                uint8_t **blocks=realloc(c->blocks,(size_t)capacity*sizeof *blocks);
                if(!blocks) return -1;
                c->blocks=blocks; c->capacity=capacity;
            }
            uint8_t *block=malloc(PCM_CACHE_BLOCK);
            if(!block) return -1;
            c->blocks[c->count++]=block;
        }
        size_t take=PCM_CACHE_BLOCK-offset;
        if(take>n) take=n;
        memcpy(c->blocks[c->count-1]+offset,data,take);
        c->size+=(uint32_t)take; data+=take; n-=take;
    }
    return 0;
}
uint8_t pcm_cache_sample(const pcm_cache_t *c, uint32_t i)
{
    return c->blocks[i/PCM_CACHE_BLOCK][i%PCM_CACHE_BLOCK];
}
void pcm_cache_free(pcm_cache_t *c)
{
    for(uint32_t i=0;i<c->count;i++) free(c->blocks[i]);
    free(c->blocks); memset(c,0,sizeof *c);
}
static void publish(void *p, size_t size)
{
#if defined(__sh__)
    uintptr_t begin=(uintptr_t)p & ~(uintptr_t)31;
    uintptr_t end=(uintptr_t)p+size;
    for(uintptr_t a=begin;a<end;a+=32)
        __asm__ volatile("ocbwb @%0" :: "r"(a) : "memory");
#else
    (void)p; (void)size;
#endif
}
void pcm_cache_publish(pcm_cache_t *c)
{
    /* Decoder writes in gint-world; sender reads in OS-world. */
    for(uint32_t i=0;i<c->count;i++) publish(c->blocks[i],PCM_CACHE_BLOCK);
    publish(c->blocks,(size_t)c->count*sizeof *c->blocks);
    publish(c,sizeof *c);
}
