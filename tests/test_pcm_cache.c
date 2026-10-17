#include "../src/pcm-cache.h"
#include <stdlib.h>
#include <assert.h>
#include <stdio.h>
static int allowance,live;
void *test_malloc(size_t n) { if(!allowance--)return NULL;void *p=malloc(n);if(p)live++;return p; }
void *test_realloc(void *p,size_t n) { if(!allowance--)return NULL;void *q=realloc(p,n);if(q && !p)live++;return q; }
void test_free(void *p) { if(p)live--;free(p); }
int main(void) {
 uint8_t bytes[100000];for(unsigned i=0;i<sizeof bytes;i++)bytes[i]=(uint8_t)i;
 for(int n=0;n<10;n++) {
  allowance=n;pcm_cache_t c={0};int r=pcm_cache_write(&c,bytes,sizeof bytes);
  if(!r) for(unsigned i=0;i<sizeof bytes;i++)assert(pcm_cache_sample(&c,i)==(uint8_t)i);
  else assert(c.size<sizeof bytes);
  pcm_cache_free(&c);assert(!live && !c.blocks && !c.size);
 }
 puts("PCM cache: allocation-failure cleanup and block boundaries PASS");
}
