#include "../src/pcm-stream.h"
#include "../src/pcm-cache.h"
#include <assert.h>
#include <stdio.h>
static int fail(void *c,const uint8_t *p,size_t n) {(void)c;(void)p;(void)n;return -1;}
int main(void) {
 int16_t data[6000];for(int i=0;i<6000;i++)data[i]=(int16_t)(i*101);
 unsigned rates[]={8000,11025,16000,22050,28800,32000,44100,48000};
 for(unsigned j=0;j<8;j++) {
  pcm_stream_t a,b;pcm_cache_t ac={0},bc={0};
  pcm_stream_init(&a,&ac,pcm_cache_write);pcm_stream_init(&b,&bc,pcm_cache_write);
  assert(!pcm_stream_push(&a,data,6000,rates[j]));
  for(unsigned k=0;k<6000;k++) assert(!pcm_stream_push(&b,data+k,1,rates[j]));
  assert(!pcm_stream_finish(&a) && !pcm_stream_finish(&b));
  assert(ac.size==(6000ull*28800+rates[j]-1)/rates[j]);
  assert(ac.size==bc.size);
  for(unsigned k=0;k<ac.size;k++) assert(pcm_cache_sample(&ac,k)==pcm_cache_sample(&bc,k));
  assert(!pcm_stream_finish(&a) && ac.size==bc.size);
  pcm_cache_free(&ac);pcm_cache_free(&bc);
 }
 pcm_stream_t s;pcm_cache_t c={0};pcm_stream_init(&s,&c,pcm_cache_write);
 assert(!pcm_stream_push(&s,data,1,44100));
 assert(pcm_stream_push(&s,data,1,22050)==-1);
 assert(!pcm_stream_finish(&s) && c.size==1);pcm_cache_free(&c);
 pcm_stream_init(&s,0,fail);assert(pcm_stream_push(&s,data,6000,22050)==-1);
 pcm_stream_init(&s,0,fail);assert(!pcm_stream_push(&s,data,1,22050));assert(pcm_stream_finish(&s)==-1);
 puts("Stream PCM: chunk invariance, exact duration, last sample, rate changes, sink errors PASS");
}
