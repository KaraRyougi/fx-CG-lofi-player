#include "flashmap.h"
#include <string.h>
int32_t flashmap_find(const uint8_t *nor, uint32_t nor_size,
    const uint8_t *data, size_t size, uint32_t expected, int (*cancel)(void))
{
    if(!size || size>FLASHMAP_CHUNK || nor_size<FLASHMAP_CHUNK) return -1;
    uint32_t count=nor_size/FLASHMAP_CHUNK;
    uint32_t slot=(expected/FLASHMAP_CHUNK)%count;
    for(uint32_t tried=0; tried<count; tried++) {
        if((tried&127)==0 && cancel && cancel()) return -1;
        uint32_t offset=slot*FLASHMAP_CHUNK;
        if(!memcmp(nor+offset,data,size)) return (int32_t)offset;
        if(++slot==count) slot=0;
    }
    return -1;
}
