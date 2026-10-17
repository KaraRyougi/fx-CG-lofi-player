#include "../src/mp3-decode.h"
#include "../src/pcm-stream.h"
#include "../src/pcm-cache.h"
#include <stdio.h>
#include <stdlib.h>
#include <assert.h>
typedef struct { FILE *f; pcm_stream_t stream; pcm_cache_t cache; int cancel,read_limit,reads; } host_t;
static int read_data(void *ctx,uint32_t pos,void *p,size_t n) {
 host_t *h=ctx;
 if(h->read_limit && ++h->reads>h->read_limit) return -1;
 return fseek(h->f,pos,SEEK_SET) || fread(p,1,n,h->f)!=n ? -1:0;
}
static int emit(void *ctx,const int16_t *p,size_t n,uint32_t rate) {
 host_t *h=ctx;return pcm_stream_push(&h->stream,p,n,rate);
}
static int progress(void *ctx,const decode_stats_t *s) {
 host_t *h=ctx;return h->cancel && s->frames>=(unsigned)h->cancel;
}
int main(int argc,char **argv) {
 if(argc<3) return 2;
 host_t h={0};h.f=fopen(argv[1],"rb");assert(h.f);
 if(argc>3) h.cancel=atoi(argv[3]);
 if(argc>4) h.read_limit=atoi(argv[4]);
 fseek(h.f,0,SEEK_END);long size=ftell(h.f);
 pcm_stream_init(&h.stream,&h.cache,pcm_cache_write);
 decode_io_t io={&h,(uint32_t)size,read_data,emit,NULL,progress};decode_stats_t stats;
 int result=mp3_decode(&io,&stats);
 if(!result) {
  assert(!pcm_stream_finish(&h.stream));
  assert(h.cache.size==(stats.samples*28800+stats.rate-1)/stats.rate);
  FILE *out=fopen(argv[2],"wb");assert(out);
  uint8_t header[44];wav_header(header,h.cache.size,28800);fwrite(header,1,44,out);
  for(uint32_t i=0;i<h.cache.size;i++) fputc(pcm_cache_sample(&h.cache,i),out);
  if(h.cache.size&1) fputc(0,out);
  fclose(out);
 }
 printf("result=%d frames=%u input=%llu rate=%u output=%u error=%s\n",result,stats.frames,(unsigned long long)stats.samples,stats.rate,h.cache.size,stats.error?stats.error:"none");
 fclose(h.f);pcm_cache_free(&h.cache);
 return result==0 ? 0 : result==1 ? 3 : 1;
}
