#include "nor-source.h"
#include "flashmap.h"
#include <string.h>
int nor_source_read(void *context,uint32_t offset,void *data,size_t n)
{
    const nor_source_t *s=context;
    if(offset>s->size || n>s->size-offset) return -1;
    uint8_t *out=data;
    while(n) {
        unsigned in_page=offset%FLASHMAP_CHUNK;
        size_t take=FLASHMAP_CHUNK-in_page;
        if(take>n) take=n;
        memcpy(out,s->nor+s->map[offset/FLASHMAP_CHUNK]+in_page,take);
        offset+=(uint32_t)take; out+=take; n-=take;
    }
    return 0;
}
static uint8_t nor_byte(const nor_source_t *s,uint32_t pos)
{
    return s->nor[s->map[pos/FLASHMAP_CHUNK]+pos%FLASHMAP_CHUNK];
}
int16_t nor_source_sample(void *context,uint32_t index)
{
    const nor_source_t *s=context;
    if(s->bits==16) {
        unsigned lo, hi;
        if(s->samples) { lo=s->samples[2*index]; hi=s->samples[2*index+1]; }
        else { uint32_t pos=s->pcm_offset+2*index; lo=nor_byte(s,pos); hi=nor_byte(s,pos+1); }
        return (int16_t)(lo | hi<<8);
    }
    unsigned v=s->samples ? s->samples[index] : nor_byte(s,s->pcm_offset+index);
    return (int16_t)(((int)v-128)<<8);
}
