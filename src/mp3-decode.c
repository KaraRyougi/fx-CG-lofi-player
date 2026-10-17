#include "mp3-decode.h"
#include "stream.h"
#include "frame.h"
#include "synth.h"
#include <stdlib.h>
#include <string.h>
#define INPUT_SIZE 8192u
/* 0.9.5: the live decoder's frame (9.3 KB, the subband samples rewritten
   every frame) sits in on-chip X/Y RAM together with libmad's
   requantization work array (vendor/libmad/layer3.c, MAD_WORK_XYRAM):
   13.9 KB of the 16 KB at 0xe500e000, saved and restored by gint around
   world switches. Target: the per-frame impulse of 261015_01/02 (NOISE.md
   item 6). libmad streams about 40 KB of work data through the cache for
   every frame, with pure store bursts (the rzero fill of xr, the overlap
   pass over the subband samples) that keep the SDRAM bus saturated for
   ~100 us at fixed points of every frame, and the I/O rail follows the
   external bus; Opus keeps its working set within a few KB. On-chip
   memory takes these off the bus. The PCM is bit-identical
   (tests/test_mp3.py, tests/test-live-stream.sh). */
#if defined(__sh__)
#define MP3_WORK_ATTR __attribute__((section(".xyram")))
#else
#define MP3_WORK_ATTR
#endif
static struct mad_frame live_frame MP3_WORK_ATTR;
typedef struct {
    struct mad_stream stream;
    struct mad_frame *frame;             /* &live_frame */
    struct mad_synth synth;
    uint8_t input[INPUT_SIZE+MAD_BUFFER_GUARD];
    int16_t pcm[1152];
} decoder_t;
/* "Decode while waiting" (0.9.4 option), MP3 side: the bit reservoir makes
   a lone frame undecodable, so the filler runs the synthesis filterbank
   (about half of a frame's work) on the last decoded frame. It reads the
   decoder's frame in place: the filler only runs while the producer waits
   for ring space, when the decoder is idle. (0.9.4 copied the 9 KB struct
   after every frame: one more SDRAM burst per frame, now gone.) */
static struct mad_synth fill_synth;
static const struct mad_frame *fill_src;
static int fill_on;
int mp3_fill_init(void)
{
    mad_synth_init(&fill_synth);
    fill_on=1; fill_src=NULL;
    return 0;
}
void mp3_fill_step(void)
{
    if(fill_on && fill_src) mad_synth_frame(&fill_synth,fill_src);
}
void mp3_fill_free(void)
{
    if(fill_on) { mad_synth_finish(&fill_synth); }   /* a no-op macro in libmad */
    fill_on=0; fill_src=NULL;
}
static int16_t pcm_s16(mad_fixed_t left,mad_fixed_t right,int stereo)
{
    int64_t sample=left;
    if(stereo) sample=(sample+right)/2;
    sample=(sample*DECODE_HEADROOM_Q15)>>15;   /* see mp3-decode.h */
    if(sample < -MAD_F_ONE) sample=-MAD_F_ONE;
    if(sample >= MAD_F_ONE) sample=MAD_F_ONE-1;
    return (int16_t)(sample >> (MAD_F_FRACBITS-15));
}
int mp3_decode(const decode_io_t *io,decode_stats_t *stats)
{
    memset(stats,0,sizeof *stats);
    stats->pcm_hash=2166136261u;
    decoder_t *d=calloc(1,sizeof *d);
    if(!d) { stats->error="Not enough RAM for decoder"; return -1; }
    d->frame=&live_frame;
    mad_stream_init(&d->stream); mad_frame_init(d->frame); mad_synth_init(&d->synth);
    uint32_t offset=0; size_t valid=0;
    int final=0,result=-1;
    uint8_t tag[10];
    if(io->size>=10) {
        if(io->read(io->context,0,tag,10)) { stats->error="MP3 read failed"; goto done; }
        if(!memcmp(tag,"ID3",3)) {
            if(tag[3]<2 || tag[3]>4 || (tag[6]|tag[7]|tag[8]|tag[9])&0x80) {
                stats->error="Invalid ID3v2 header"; goto done;
            }
            offset=10u+((uint32_t)tag[6]<<21)+((uint32_t)tag[7]<<14)+
                ((uint32_t)tag[8]<<7)+tag[9];
            if(tag[3]==4 && (tag[5]&0x10)) offset+=10;
            if(offset>io->size) { stats->error="Truncated ID3v2 tag"; goto done; }
        }
    }
    for(;;) {
        if(io->progress && io->progress(io->context,stats)) {
            stats->cancelled=1; result=1; break;
        }
        if(!d->stream.buffer || d->stream.error==MAD_ERROR_BUFLEN) {
            if(final) {
                if(!stats->frames) stats->error="No complete MP3 audio frames";
                else if(d->stream.next_frame && d->stream.bufend-d->stream.next_frame>MAD_BUFFER_GUARD)
                    stats->error="Truncated final MP3 frame";
                else result=0;
                break;
            }
            size_t keep=d->stream.next_frame ? (size_t)(d->stream.bufend-d->stream.next_frame) : 0;
            if(keep>=INPUT_SIZE) { stats->error="MP3 frame exceeds input buffer"; break; }
            if(keep) memmove(d->input,d->stream.next_frame,keep);
            size_t n=io->size-offset;
            if(n>INPUT_SIZE-keep) n=INPUT_SIZE-keep;
            if(n && io->read(io->context,offset,d->input+keep,n)) {
                stats->error="MP3 read failed"; break;
            }
            offset+=(uint32_t)n; valid=keep+n;
            if(offset==io->size) {
                memset(d->input+valid,0,MAD_BUFFER_GUARD);
                valid+=MAD_BUFFER_GUARD; final=1;
            }
            mad_stream_buffer(&d->stream,d->input,(unsigned long)valid);
        }
        uint64_t start=io->clock_us ? io->clock_us(io->context) : 0;
        int failed=mad_frame_decode(d->frame,&d->stream);
        if(!failed && d->frame->header.layer!=MAD_LAYER_III) {
            stats->error="Only MPEG Layer III is supported"; break;
        }
        if(!failed) {
            mad_synth_frame(&d->synth,d->frame);
            if(fill_on) fill_src=d->frame;      /* the wait filler synthesizes it again */
        }
        uint64_t us=io->clock_us ? io->clock_us(io->context)-start : 0;
        stats->decode_us+=us;
        if(failed) {
            stats->last_error=d->stream.error;
            if(d->stream.error==MAD_ERROR_BUFLEN) continue;
            if(MAD_RECOVERABLE(d->stream.error)) { stats->recoveries++; continue; }
            stats->error="MP3 decoder error"; break;
        }
        if(us>stats->max_frame_us) stats->max_frame_us=us;
        unsigned rate=d->synth.pcm.samplerate, count=d->synth.pcm.length;
        unsigned channels=d->synth.pcm.channels;
        if(!rate || count>1152 || (channels!=1 && channels!=2)) {
            stats->error="Invalid decoded audio format"; break;
        }
        if(stats->frames && rate!=stats->rate) {
            stats->error="Sample rate changed mid-stream"; break;
        }
        stats->rate=rate; stats->channels=channels;
        for(unsigned i=0;i<count;i++)
            d->pcm[i]=pcm_s16(d->synth.pcm.samples[0][i],d->synth.pcm.samples[1][i],channels==2);
        if(io->emit && io->emit(io->context,d->pcm,count,rate)) {
            stats->error="PCM output failed"; break;
        }
        for(unsigned i=0;i<count;i++) stats->pcm_hash=(stats->pcm_hash^(uint16_t)d->pcm[i])*16777619u;
        stats->samples+=count; stats->frames++;
    }
done:
    fill_src=NULL;
    mad_synth_finish(&d->synth); mad_frame_finish(d->frame); mad_stream_finish(&d->stream);
    free(d); return result;
}
static void le32(uint8_t *p,uint32_t n)
{
    p[0]=(uint8_t)n; p[1]=(uint8_t)(n>>8); p[2]=(uint8_t)(n>>16); p[3]=(uint8_t)(n>>24);
}
void wav_header(uint8_t h[44],uint32_t samples,uint32_t rate)
{
    memset(h,0,44); memcpy(h,"RIFF",4); le32(h+4,36+samples+(samples&1));
    memcpy(h+8,"WAVEfmt ",8); le32(h+16,16); h[20]=1; h[22]=1;
    le32(h+24,rate); le32(h+28,rate); h[32]=1; h[34]=8;
    memcpy(h+36,"data",4); le32(h+40,samples);
}
