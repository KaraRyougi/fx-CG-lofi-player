/* Host driver for src/opus-file.c: decodes an Ogg Opus file to raw s16 mono
   24 kHz. Usage: opus-host in.opus out.s16 [start_ms] [packets.csv]
   With a CSV path, one row per packet (packet,samples,us) with the decode
   time of opus_decode() alone (CLOCK_MONOTONIC), for the encoder matrix of
   tests/profile-opus.sh. Built with -DOPUS_HOST_CRC_FLAG against the
   campaign tree, OPUS_CRC=0 in the environment skips the Ogg page CRC. */
#include "opus-file.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#ifdef OPUS_HOST_CRC_FLAG
extern int opus_page_crc;
#endif
static FILE *in,*out,*csv; static uint8_t *data; static uint32_t size;
static uint64_t prev_us; static unsigned prev_frames; static size_t last_n;
static uint32_t *us_log; static size_t us_count, us_cap;
static int rd(void *c,uint32_t pos,void *p,size_t n){(void)c; if(pos>size||size-pos<n) return -1; memcpy(p,data+pos,n); return 0;}
static int em(void *c,const int16_t *p,size_t n,uint32_t r){(void)c;(void)r; last_n=n; fwrite(p,2,n,out); return 0;}
static uint64_t clk(void *c){(void)c; struct timespec t; clock_gettime(CLOCK_MONOTONIC,&t); return (uint64_t)t.tv_sec*1000000ull+(uint64_t)t.tv_nsec/1000u;}
static int prog(void *c,const decode_stats_t *s)
{
    (void)c;
    if(s->frames!=prev_frames) {              /* one packet decoded since the last call */
        uint64_t us=s->decode_us-prev_us;
        if(csv) fprintf(csv,"%u,%zu,%llu\n",s->frames,last_n,(unsigned long long)us);
        if(us_count==us_cap) { us_cap=us_cap?us_cap*2:4096; us_log=realloc(us_log,us_cap*sizeof *us_log); }
        us_log[us_count++]=(uint32_t)us;
        prev_us=s->decode_us; prev_frames=s->frames;
    }
    return 0;
}
static int cmp_u32(const void *a,const void *b){ uint32_t x=*(const uint32_t*)a,y=*(const uint32_t*)b; return x<y?-1:x>y; }
int main(int argc,char**argv){
  if(argc<3) { fprintf(stderr,"usage: opus-host in.opus out.s16 [start_ms] [packets.csv]\n"); return 2; }
  in=fopen(argv[1],"rb"); if(!in) return 1; fseek(in,0,SEEK_END); size=ftell(in); fseek(in,0,SEEK_SET); data=malloc(size); if(fread(data,1,size,in)!=size) return 1;
  out=fopen(argv[2],"wb"); uint32_t start=argc>3?atoi(argv[3]):0;
  if(argc>4) { csv=fopen(argv[4],"w"); if(csv) fprintf(csv,"packet,samples,us\n"); }
#ifdef OPUS_HOST_CRC_FLAG
  const char *crc=getenv("OPUS_CRC"); if(crc) opus_page_crc=atoi(crc);
#endif
  decode_io_t io={0}; io.context=0; io.size=size; io.read=rd; io.emit=em; io.clock_us=clk; io.progress=prog;
  decode_stats_t st; clock_t t=clock();
  int r=opus_file_decode(&io,&st,start);
  double sec=(double)(clock()-t)/CLOCKS_PER_SEC;
  double mean=st.frames?(double)st.decode_us/st.frames:0, p99=0; uint32_t mx=0;
  if(us_count) { qsort(us_log,us_count,sizeof *us_log,cmp_u32); mx=us_log[us_count-1]; p99=us_log[(us_count*99)/100]; }
  double pkt_ms=st.frames?st.samples/24.0/st.frames:0;     /* mean packet duration, ms */
  printf("result %d err %s packets %u samples %llu (%.2f s) ch %u recov %u cpu %.2f s; length_ms %u; "
         "dec mean %.1f us max %u us p99 %.0f us, packet %.1f ms, duty %.3f, pcm_hash %08x\n",
         r,st.error?st.error:"-",st.frames,(unsigned long long)st.samples,st.samples/24000.0,st.channels,st.recoveries,sec,
         opus_file_length_ms(&io),mean,mx,p99,pkt_ms,pkt_ms>0?mean/(pkt_ms*1000.0):0,st.pcm_hash);
  if(csv) fclose(csv);
  fclose(out); return 0;
}
