#include "../src/sdm-stream.h"
#include "../src/tx-ring.h"
#include "../src/mp3-decode.h"
#include <assert.h>
#include <stdio.h>
#include <stdlib.h>
typedef struct { tx_ring_t ring; uint64_t count,hash; FILE *file; uint32_t file_size; sdm_stream_t stream; } host_t;
static void drain(host_t *h,unsigned n) {
 if(n>tx_ring_used(&h->ring)) n=tx_ring_used(&h->ring);
 while(n--) { h->hash=(h->hash^h->ring.bytes[h->ring.read&TX_RING_MASK])*1099511628211ULL;h->ring.read++;h->count++; }
}
static int write_data(void *ctx,unsigned frame) {
 host_t *h=ctx;uint8_t p[SDM_FRAME_BYTES];sdm_frame_write(p,frame);
 while(tx_ring_write(&h->ring,p,SDM_FRAME_BYTES)) drain(h,997);
 return 0;
}
static int16_t fetch(void *ctx,uint32_t i) { return ((int16_t *)ctx)[i]; }
static int read_data(void *ctx,uint32_t pos,void *p,size_t n) {
 host_t *h=ctx;uint8_t *out=p;
 while(n) {
  uint32_t offset=pos%h->file_size;size_t take=h->file_size-offset;if(take>n)take=n;
  if(fseek(h->file,offset,SEEK_SET)||fread(out,1,take,h->file)!=take)return -1;
  out+=take;pos+=(uint32_t)take;n-=take;
 }
 return 0;
}
static int emit(void *ctx,const int16_t *p,size_t n,uint32_t rate) {
 host_t *h=ctx;return sdm_stream_push(&h->stream,p,n,rate);
}
int main(int argc,char **argv) {
 assert(argc==2);host_t *h=calloc(1,sizeof *h);assert(h);
 int16_t samples[6000]; for(unsigned i=0;i<6000;i++) samples[i]=(int16_t)(i*7919u);
 unsigned rates[]={8000,11025,16000,22050,24000,28800,32000,44100,48000};
 for(unsigned k=0;k<sizeof rates/sizeof *rates;k++) {
  *h=(host_t){0};h->ring.read=h->ring.write=UINT32_MAX-100;
  sdm_stream_init(&h->stream,10,h,write_data);
  for(unsigned i=0;i<6000;) { unsigned n=(i%113)+1;if(n>6000-i)n=6000-i;
   assert(!sdm_stream_push(&h->stream,samples+i,n,rates[k]));i+=n; }
  assert(!sdm_stream_finish(&h->stream));assert(!sdm_stream_finish(&h->stream));drain(h,TX_RING_SIZE);
  assert(h->count==(((uint64_t)6000*SDM_RATE+rates[k]-1)/rates[k])*SDM_FRAME_BYTES);
  /* Since 0.9.1 only 28 800 Hz is bit-identical to the WAV-path modulator
    (sdm_generate interpolates linearly; the stream uses a polyphase FIR,
    tested separately in tests/test_resampler.c). */
 if(rates[k]==SDM_RATE) {
  sdm_t ref={.rate=rates[k],.count=6000,.volume=10,.fetch=fetch,.context=samples};sdm_start(&ref);
  uint64_t hash=0,count=0;uint8_t bytes[257];unsigned n;
  while((n=sdm_generate(&ref,bytes,sizeof bytes))) for(unsigned j=0;j<n;j++) {hash=(hash^bytes[j])*1099511628211ULL;count++;}
  assert(hash==h->hash && count==h->count);
 }
 }
 /* The player's ring writer (tx_ring_put_frame, 1.1.1: words generated in
    place; movca.l/ocbp on the SH) gives the bytewise frame for every frame
    code, across the end of the ring, and advances by one frame. */
 {
  *h=(host_t){0};h->ring.read=h->ring.write=UINT32_MAX-64*300+1;
  h->ring.read=h->ring.write=(h->ring.write+63)&~63u;            /* frame-aligned, wraps the counter */
  unsigned codes=0;
  for(int sh=0;sh<2;sh++) for(unsigned n=0;n<=SDM_FRAME_BITS;n++) {
   if(sh) break;
   unsigned frame=n;
   uint8_t want[SDM_FRAME_BYTES];
   if(!sh) sdm_frame_bytes(n,want); else sdm_frame_write(want,frame);
   memset(h->ring.bytes+(h->ring.write&TX_RING_MASK),0x5a,SDM_FRAME_BYTES);
   uint32_t w=h->ring.write;tx_ring_put_frame(&h->ring,frame);
   assert(h->ring.write==w+SDM_FRAME_BYTES);
   assert(!memcmp(h->ring.bytes+(w&TX_RING_MASK),want,SDM_FRAME_BYTES));
   h->ring.read=h->ring.write;codes++;
  }
  printf("PASS: ring frame writer, %u frame codes\n",codes);
 }
 *h=(host_t){0};h->file=fopen(argv[1],"rb");assert(h->file);fseek(h->file,0,SEEK_END);long size=ftell(h->file);
 sdm_stream_init(&h->stream,16,h,write_data);
 h->file_size=(uint32_t)size;
 decode_io_t io={h,(uint32_t)size*160,read_data,emit,NULL,NULL};decode_stats_t stats;
 uint64_t samples_total=0;
 assert(!mp3_decode(&io,&stats));samples_total=stats.samples;
 assert(!sdm_stream_finish(&h->stream));drain(h,TX_RING_SIZE);
 assert(h->count==((samples_total*SDM_RATE+stats.rate-1)/stats.rate)*SDM_FRAME_BYTES);
 printf("PASS: frame counts for 9 rates, 28.8 kHz bit-exact; %.1f seconds decoded, %llu bytes consumed, fixed host state %zu bytes\n",(double)samples_total/stats.rate,(unsigned long long)h->count,sizeof *h);
 fclose(h->file);free(h);
}
