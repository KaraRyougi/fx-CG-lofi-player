#include "../src/wav.h"
#include <assert.h>
#include <stdio.h>
#include <string.h>
#include <stdlib.h>

static const uint8_t valid[] = {
    'R','I','F','F',50,0,0,0,'W','A','V','E',
    'J','U','N','K',1,0,0,0,99,0,
    'f','m','t',' ',16,0,0,0,1,0,1,0,
    0x40,0x1f,0,0,0x40,0x1f,0,0,1,0,8,0,
    'd','a','t','a',4,0,0,0,0,128,255,128
};
int main(int argc, char **argv)
{
    wav_t wav;
    assert(!wav_parse(valid,sizeof valid,&wav));
    assert(wav.rate==8000 && wav.count==4 && wav.bits==8 && wav.samples[2]==255);
    /* Every physical truncation must fail, including chunk header/padding. */
    for(size_t n=0;n<sizeof valid;n++) assert(wav_parse(valid,n,&wav));
    uint8_t b[sizeof valid];
    memcpy(b,valid,sizeof b); b[32]=2; /* Stereo. */
    assert(wav_parse(b,sizeof b,&wav));
    memcpy(b,valid,sizeof b); b[44]=16; /* 16-bit with 8-bit byte rate/align. */
    assert(wav_parse(b,sizeof b,&wav));
    memcpy(b,valid,sizeof b); b[44]=16; b[42]=2; b[38]=0x80; b[39]=0x3e; /* 16-bit, 2 samples. */
    assert(!wav_parse(b,sizeof b,&wav));
    assert(wav.bits==16 && wav.count==2 && wav.rate==8000);
    assert((int16_t)(wav.samples[2]|wav.samples[3]<<8)==(int16_t)0x80ff);
    memcpy(b,valid,sizeof b); b[44]=12; /* Unsupported depth. */
    assert(wav_parse(b,sizeof b,&wav));
    memcpy(b,valid,sizeof b); b[50]=255; /* Oversized data chunk. */
    assert(wav_parse(b,sizeof b,&wav));
    memcpy(b,valid,sizeof b); b[16]=255; /* Oversized unknown chunk. */
    assert(wav_parse(b,sizeof b,&wav));
    memcpy(b,valid,sizeof b); b[38]=0; /* Wrong byte rate. */
    assert(wav_parse(b,sizeof b,&wav));
    /* Encoding preserves the duty of all 256 possible unsigned samples. */
    for(unsigned sample=0;sample<256;sample++) {
        uint8_t encoded[32]; cgplayer_encode((uint8_t)sample,encoded);
        unsigned ones=0;
        for(unsigned j=0;j<32;j++)
            for(unsigned bit=0;bit<8;bit++) ones+=(encoded[j]>>bit)&1;
        assert(ones==sample);
    }
    puts("WAV parser and all 256 sample encodings passed");
    if(argc==2) {
        FILE *file=fopen(argv[1],"rb"); assert(file);
        assert(!fseek(file,0,SEEK_END));
        long size=ftell(file); assert(size>0);
        rewind(file);
        uint8_t *data=malloc((size_t)size); assert(data);
        assert(fread(data,1,(size_t)size,file)==(size_t)size);
        fclose(file);
        const char *problem=wav_parse(data,(size_t)size,&wav);
        if(problem) fprintf(stderr,"%s\n",problem);
        assert(!problem);
        printf("Input WAV: %ld bytes, %u Hz, %u-bit, %u samples\n",size,wav.rate,wav.bits,wav.count);
        free(data);
    }
    return 0;
}
