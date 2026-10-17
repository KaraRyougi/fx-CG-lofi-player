/* Host test of the polyphase resampler in sdm-stream.c (0.9.1). */
#include "../src/sdm-stream.h"
#include <stdio.h>
#include <stdlib.h>
#include <math.h>
#include <assert.h>
static double *taps_out; static size_t ntap;
static void tap(void *c,int32_t v){(void)c; taps_out[ntap++]=v;}
static uint64_t frames;
static int wr(void *c,unsigned frame){(void)c;(void)frame; frames++; return 0;}
static double run(uint32_t rate,double f,double amp,size_t n,double *alias_level){
  sdm_stream_t s; frames=0; ntap=0; taps_out=malloc(sizeof(double)*(n*SDM_RATE/rate+100));
  sdm_stream_set_tap(tap,0); sdm_stream_init(&s,16,0,wr);
  int16_t buf[100];
  for(size_t i=0;i<n;){ size_t k=n-i<100?n-i:100; for(size_t j=0;j<k;j++) buf[j]=(int16_t)lrint(amp*32767*sin(2*M_PI*f*(i+j)/rate)); assert(!sdm_stream_push(&s,buf,k,rate)); i+=k; }
  assert(!sdm_stream_finish(&s));
  uint64_t expect=((uint64_t)n*SDM_RATE+rate-1)/rate; assert(frames==expect && ntap==expect);
  /* compare with the ideal band-limited value at t_k = k*rate/28800 (skip edges) */
  double es=0,ss=0; size_t a=ntap/10,b=ntap*9/10;
  double fo=f; int aliased=0;
  if(f>SDM_RATE/2.0){ aliased=1; }
  for(size_t k=a;k<b;k++){ double t=(double)k*rate/SDM_RATE; double ideal=aliased?0:amp*32767*sin(2*M_PI*fo*t/rate); double e=taps_out[k]-ideal; es+=e*e; ss+=ideal*ideal; }
  if(alias_level) *alias_level=10*log10(es/(b-a)/(32767.0*32767*amp*amp/2));
  free(taps_out);
  return aliased?0:10*log10(ss/es);
}
int main(){
  uint32_t rates[]={8000,11025,16000,22050,24000,32000,44100,48000};
  for(unsigned r=0;r<8;r++){
    double f1=1000, f2=rates[r]*0.4;
    if(f2>11000) f2=11000;
    double a1=run(rates[r],f1,0.5,rates[r]*2,0), a2=run(rates[r],f2,0.5,rates[r]*2,0);
    printf("%5u Hz: SNR 1 kHz %.1f dB, %.0f Hz %.1f dB\n",rates[r],a1,f2,a2);
    assert(a1>70 && a2>45);
  }
  double al;
  run(44100,18000,0.5,88200,&al); printf("44.1 kHz input, 18 kHz tone (would alias to 10.8 kHz): residual %.1f dB re tone\n",al); assert(al<-70);
  run(48000,20000,0.5,96000,&al); printf("48 kHz input, 20 kHz tone (would alias to 8.8 kHz): residual %.1f dB re tone\n",al); assert(al<-70);
  puts("PASS: resampler accuracy and alias rejection");
  return 0;
}
