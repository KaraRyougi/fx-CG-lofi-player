#include "../src/flashmap.h"
#include <assert.h>
#include <string.h>
#include <stdio.h>
static int cancelled(void) { return 1; }
int main(void)
{
    uint8_t nor[4096], first[512], second[512];
    memset(nor,0xff,sizeof nor);
    for(unsigned i=0;i<512;i++) { first[i]=(uint8_t)(i*13); second[i]=(uint8_t)(i*7); }
    memcpy(nor+3072,first,512); memcpy(nor+512,second,512);
    assert(flashmap_find(nor,sizeof nor,first,512,0,NULL)==3072);
    assert(flashmap_find(nor,sizeof nor,second,512,3584,NULL)==512);
    assert(flashmap_find(nor,sizeof nor,second,17,512,NULL)==512);
    first[0]^=1;
    assert(flashmap_find(nor,sizeof nor,first,512,0,NULL)==-1);
    assert(flashmap_find(nor,sizeof nor,second,512,0,cancelled)==-1);
    assert(flashmap_find(nor,sizeof nor,second,513,0,NULL)==-1);
    puts("Fragmented NOR, wrapped search, partial tail and cancellation passed");
}
