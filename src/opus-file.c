/* Ogg Opus reader for Lo-Fi Player 100: Ogg page/packet layer, granule-based
   seeking and sample-exact trimming around fixed-point libopus. Random
   access goes through io->read (memory-mapped NOR on the calculator). */
#include "opus-file.h"
#include <opus.h>
#include <stdlib.h>
#include <string.h>

#define PKT_MAX 16384u          /* larger audio packets are treated as lost */
#define PREROLL_48K 3840        /* RFC 7845: decode >= 80 ms before a seek target */
#define MAX_FRAME_24K 2880      /* 120 ms at 24 kHz */

typedef struct {
    const decode_io_t *io;
    uint32_t serial; int have_serial;
    /* current page */
    uint32_t page_off, body_off, body_len;
    uint8_t nseg, type, lace[255];
    int64_t granule;
    unsigned seg;               /* next segment */
    uint32_t seg_off;           /* file offset of next segment */
    uint32_t next_page;         /* offset after the current page */
    int have_page;
    /* packet assembly */
    uint8_t *pkt; uint32_t plen;
    int partial, oversize, skipping;
} ogg_t;

static uint32_t crc_table[256];
static void crc_init(void)
{
    if(crc_table[1]) return;
    for(uint32_t i=0;i<256;i++) {
        uint32_t r=i<<24;
        for(int k=0;k<8;k++) r=(r&0x80000000u) ? (r<<1)^0x04c11db7u : r<<1;
        crc_table[i]=r;
    }
}
static uint32_t crc_update(uint32_t crc, const uint8_t *p, uint32_t n)
{
    while(n--) crc=(crc<<8)^crc_table[((crc>>24)^*p++)&0xffu];
    return crc;
}
static uint32_t le32(const uint8_t *p) { return p[0]|(uint32_t)p[1]<<8|(uint32_t)p[2]<<16|(uint32_t)p[3]<<24; }
/* "Decode while waiting" (0.9.4 option): a second decoder instance decodes
   the last real packet again and again while the producer waits for ring
   space, so the CPU load no longer switches between decoding and waiting
   at the packet rate (50 Hz for 20 ms packets). 261014_49/50: the 50 Hz
   family -15..-18 dB, broadband +2 dB, and the supply hump moves from 17 to
   ~9-11 kHz. */
#define FILL_PKT_MAX 1536u
static uint8_t fill_pkt[FILL_PKT_MAX];
static uint32_t fill_len;
static OpusDecoder *fill_dec;
static int16_t *fill_pcm;
static volatile int fill_sink;
int opus_fill_init(void)
{
    fill_len=0;
    fill_dec=malloc((size_t)opus_decoder_get_size(1));
    fill_pcm=malloc(2880u*sizeof *fill_pcm);
    if(!fill_dec || !fill_pcm || opus_decoder_init(fill_dec,OPUS_FILE_RATE,1)!=OPUS_OK) { opus_fill_free(); return -1; }
    return 0;
}
void opus_fill_step(void)
{
    if(fill_dec && fill_len) fill_sink=opus_decode(fill_dec,fill_pkt,(opus_int32)fill_len,fill_pcm,2880,0);
}
void opus_fill_free(void)
{
    free(fill_dec); free(fill_pcm); fill_dec=NULL; fill_pcm=NULL; fill_len=0;
}

/* Read and CRC-check the page at off into o (header only; body stays in
   the file). Returns 0 if valid. */
static int load_page(ogg_t *o, uint32_t off)
{
    const decode_io_t *io=o->io;
    uint8_t h[27];
    if(off>io->size || io->size-off<27 || io->read(io->context,off,h,27)) return -1;
    if(memcmp(h,"OggS",4) || h[4]!=0) return -1;
    unsigned nseg=h[26];
    if(io->size-off-27<nseg || io->read(io->context,off+27,o->lace,nseg)) return -1;
    uint32_t body=0;
    for(unsigned i=0;i<nseg;i++) body+=o->lace[i];
    uint32_t hdr=27+nseg;
    if(io->size-off-hdr<body) return -1;
    /* CRC over header (with the CRC field zeroed), lacing and body */
    uint32_t want=le32(h+22);
    uint8_t hz[27]; memcpy(hz,h,27); memset(hz+22,0,4);
    uint32_t crc=crc_update(0,hz,27);
    crc=crc_update(crc,o->lace,nseg);
    uint8_t buf[256];
    for(uint32_t done=0;done<body;) {
        uint32_t n=body-done; if(n>sizeof buf) n=sizeof buf;
        if(io->read(io->context,off+hdr+done,buf,n)) return -1;
        crc=crc_update(crc,buf,n); done+=n;
    }
    if(crc!=want) return -1;
    o->page_off=off; o->body_off=off+hdr; o->body_len=body;
    o->nseg=(uint8_t)nseg; o->type=h[5];
    o->granule=(int64_t)((uint64_t)le32(h+6)|(uint64_t)le32(h+10)<<32);
    o->seg=0; o->seg_off=o->body_off; o->next_page=o->body_off+body;
    if(!o->have_serial) { o->serial=le32(h+14); o->have_serial=1; }
    else if(le32(h+14)!=o->serial) return 1;          /* other stream */
    o->have_page=1;
    return 0;
}
/* First valid page of our stream at or after off; -1 if none. */
static int64_t sync_page(ogg_t *o, uint32_t off)
{
    const decode_io_t *io=o->io;
    uint8_t buf[512];
    while(off+27<=io->size) {
        uint32_t n=io->size-off; if(n>sizeof buf) n=sizeof buf;
        if(io->read(io->context,off,buf,n)) return -1;
        for(uint32_t i=0;i+4<=n;i++) {
            if(buf[i]!='O' || memcmp(buf+i,"OggS",4)) continue;
            int r=load_page(o,off+i);
            if(r==0) return off+i;
        }
        if(n<sizeof buf) break;
        off+=n-3;
    }
    return -1;
}
/* Next complete packet of our stream. Returns 1 with o->pkt/plen, 0 at
   end of stream, -1 on a read error. o->oversize marks a packet that did
   not fit (contents dropped). Continuation data whose start we never saw
   (after a seek) is skipped. */
static int next_packet(ogg_t *o)
{
    for(;;) {
        if(!o->have_page || o->seg>=o->nseg) {
            if(o->have_page && (o->type&4)) return 0;     /* EOS */
            uint32_t off=o->have_page ? o->next_page : 0;
            int r;
            for(;;) {
                r=load_page(o,off);
                if(r==1) { off=o->next_page; o->have_page=0; continue; }
                break;
            }
            if(r) {
                int64_t s=sync_page(o,off);
                if(s<0) return 0;                         /* end of file */
            }
            if(o->type&1) { if(!o->partial) o->skipping=1; }
            else if(o->partial) { o->partial=0; o->plen=0; o->oversize=0; } /* lost tail */
            return 2;                                     /* new page loaded */
        }
        uint8_t l=o->lace[o->seg];
        if(!o->skipping) {
            if(o->plen+l>PKT_MAX) o->oversize=1;
            else if(l && o->io->read(o->io->context,o->seg_off,o->pkt+o->plen,l)) return -1;
            if(!o->oversize) o->plen+=l;
        }
        o->seg_off+=l; o->seg++;
        if(l<255) {
            if(o->skipping) { o->skipping=0; o->plen=0; o->oversize=0; continue; }
            o->partial=0;
            return 1;
        }
        o->partial=!o->skipping;
    }
}
/* Sum of 48 kHz durations of the packets that complete on the current page
   and will be decoded (a continued packet counts only if we hold its
   start). Used to place the first packet of a page on the timeline. */
static int64_t page_packets_48k(ogg_t *o)
{
    int64_t sum=0; uint32_t off=o->body_off;
    int cont=(o->type&1)!=0, have_start=cont ? (o->partial && !o->oversize) : 1;
    uint8_t first[2]={0,0}; unsigned got=0;
    if(cont && have_start) { got=o->plen>2 ? 2 : o->plen; memcpy(first,o->pkt,got); }
    uint32_t len=cont ? o->plen : 0;
    for(unsigned i=0;i<o->nseg;i++) {
        uint8_t l=o->lace[i];
        if(have_start && got<2) {
            uint32_t take=2-got; if(take>l) take=l;
            if(take && o->io->read(o->io->context,off,first+got,take)) return -1;
            got+=take;
        }
        off+=l; len+=l;
        if(l<255) {
            if(have_start && got) {
                int n=opus_packet_get_nb_samples(first,(opus_int32)(len<2?len:2),48000);
                if(n>0) sum+=n;
            }
            have_start=1; got=0; len=0;
        }
    }
    return sum;
}
static int read_head(ogg_t *o, unsigned *channels, unsigned *pre_skip, int *gain, uint32_t *audio_start)
{
    int r;
    do r=next_packet(o); while(r==2);
    if(r!=1 || o->plen<19 || memcmp(o->pkt,"OpusHead",8) || (o->pkt[8]>>4)!=0) return -1;
    *channels=o->pkt[9]; *pre_skip=o->pkt[10]|o->pkt[11]<<8;
    *gain=(int16_t)(o->pkt[16]|o->pkt[17]<<8);
    if(o->pkt[18]!=0 || *channels<1 || *channels>2) return -2;      /* mapping family 0 only */
    o->plen=0;
    do r=next_packet(o); while(r==2);                              /* OpusTags (may be oversize) */
    if(r!=1) return -1;
    o->plen=0; o->oversize=0;
    *audio_start=o->next_page;     /* audio starts on a fresh page (RFC 7845 3) */
    o->seg=o->nseg;                /* nothing else on the tags page */
    return 0;
}
uint32_t opus_file_length_ms(const decode_io_t *io)
{
    crc_init();
    ogg_t o={0}; o.io=io;
    o.pkt=malloc(PKT_MAX); if(!o.pkt) return 0;
    unsigned ch,pre; int gain; uint32_t audio;
    uint32_t ms=0;
    if(!read_head(&o,&ch,&pre,&gain,&audio)) {
        uint32_t from=io->size>65536+audio ? io->size-65536 : audio;
        int64_t last=-1; int64_t off=sync_page(&o,from);
        while(off>=0) {
            if(o.granule>=0) last=o.granule;
            if(o.type&4) break;
            off=sync_page(&o,o.next_page);
        }
        if(last>pre) ms=(uint32_t)((last-pre)/48);
    }
    free(o.pkt);
    return ms;
}
int opus_file_decode(const decode_io_t *io, decode_stats_t *stats, uint32_t start_ms)
{
    memset(stats,0,sizeof *stats);
    stats->pcm_hash=2166136261u;
    crc_init();
    int result=-1;
    ogg_t o={0}; o.io=io;
    OpusDecoder *dec=NULL;
    int16_t *pcm=NULL;
    o.pkt=malloc(PKT_MAX);
    dec=malloc((size_t)opus_decoder_get_size(1));
    pcm=malloc(MAX_FRAME_24K*sizeof *pcm);
    if(!o.pkt || !dec || !pcm) { stats->error="Not enough RAM for Opus decoder"; goto done; }
    unsigned channels,pre_skip; int gain; uint32_t audio_start;
    int h=read_head(&o,&channels,&pre_skip,&gain,&audio_start);
    if(h==-2) { stats->error="Only mono/stereo Ogg Opus is supported"; goto done; }
    if(h) { stats->error="Not an Ogg Opus file"; goto done; }
    if(opus_decoder_init(dec,OPUS_FILE_RATE,1)!=OPUS_OK) { stats->error="Opus decoder init failed"; goto done; }
    if(gain) opus_decoder_ctl(dec,OPUS_SET_GAIN(gain));
    stats->rate=OPUS_FILE_RATE; stats->channels=channels;

    int64_t start48=(int64_t)start_ms*48, end48=INT64_MAX;
    /* Seek: last page whose granule leaves >= 80 ms of pre-roll. */
    if(start48>0) {
        int64_t goal=start48+pre_skip-PREROLL_48K;
        uint32_t best=0, a=audio_start, b=io->size;
        while(goal>0 && b>a && b-a>4096) {
            uint32_t mid=a+(b-a)/2;
            int64_t off=sync_page(&o,mid);
            while(off>=0 && o.granule<0 && (uint32_t)off<b) off=sync_page(&o,o.next_page);
            if(off<0 || (uint32_t)off>=b) { b=mid; continue; }
            if(o.granule<=goal) { best=(uint32_t)off; a=o.next_page; }
            else b=mid;
        }
        if(best) {
            /* refine forward page by page */
            int64_t off=sync_page(&o,best); uint32_t last=best;
            while(off>=0) {
                if(o.granule>goal) break;
                if(o.granule>=0) last=(uint32_t)off;
                if(o.type&4) break;
                off=sync_page(&o,o.next_page);
            }
            load_page(&o,last);
            o.seg=o.nseg;              /* resume after this page */
            o.partial=0; o.plen=0; o.oversize=0; o.skipping=0;
        }
    }
    int64_t pos=-1;                    /* 48 kHz granule of next packet start */
    int last24=480;                    /* duration of the last packet, for PLC */
    for(;;) {
        if(io->progress && io->progress(io->context,stats)) { stats->cancelled=1; result=1; break; }
        int r=next_packet(&o);
        if(r<0) { stats->error="Opus read failed"; break; }
        if(r==0) { result=0; break; }
        if(r==2) {
            if(o.granule>=0) {
                if(pos<0) pos=o.granule-page_packets_48k(&o);
                if(o.type&4) end48=o.granule-pre_skip;
            }
            continue;
        }
        if(pos<0) pos=0;               /* no granule seen yet: assume start */
        uint64_t t0=io->clock_us ? io->clock_us(io->context) : 0;
        int n;
        if(o.oversize) { n=opus_decode(dec,NULL,0,pcm,last24,0); o.oversize=0; }
        else {
            int d48=opus_packet_get_nb_samples(o.pkt,(opus_int32)o.plen,48000);
            if(fill_dec && d48>0 && o.plen<=FILL_PKT_MAX) { memcpy(fill_pkt,o.pkt,o.plen); fill_len=o.plen; }
            n=d48>0 ? opus_decode(dec,o.pkt,(opus_int32)o.plen,pcm,d48/2,0) : OPUS_INVALID_PACKET;
            if(n<0) { stats->recoveries++; n=d48>0 ? opus_decode(dec,NULL,0,pcm,d48/2,0) : 0; }
        }
        o.plen=0;
        uint64_t us=io->clock_us ? io->clock_us(io->context)-t0 : 0;
        stats->decode_us+=us; if(us>stats->max_frame_us) stats->max_frame_us=us;
        if(n<=0) continue;
        last24=n;
        stats->frames++;
        /* emit the part of [pos, pos+2n) inside [start48, end48) */
        int64_t idx=pos-pre_skip;
        int64_t lo=start48>idx ? (start48-idx+1)/2 : 0;
        int64_t hi=end48<idx+2*(int64_t)n ? (end48-idx+1)/2 : n;
        if(hi<0) hi=0;
        pos+=2*(int64_t)n;
        if(hi>lo) {
            if(io->emit && io->emit(io->context,pcm+lo,(size_t)(hi-lo),OPUS_FILE_RATE)) {
                stats->error="PCM output failed"; break;
            }
            for(int64_t i=lo;i<hi;i++) stats->pcm_hash=(stats->pcm_hash^(uint16_t)pcm[i])*16777619u;
            stats->samples+=(uint64_t)(hi-lo);
        }
        if(idx+2*(int64_t)n>=end48) { result=0; break; }
    }
done:
    free(pcm); free(dec); free(o.pkt);
    return result;
}
