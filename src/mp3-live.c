#include "mp3-live.h"
#include "mp3-decode.h"
#include "opus-file.h"
#include "sdm-stream.h"
#include "tx-feed.h"
#include "wav.h"
#include "ui.h"
#include "platform.h"
#include <gint/gint.h>
#include <gint/timer.h>
#include <gint/keyboard.h>
#include <gint/display.h>
#include <gint/clock.h>
#include <gint/mpu/cpg.h>
#include <stdlib.h>
#include <stdio.h>
#include <limits.h>
#include <string.h>
#define REG16(n) (*(volatile uint16_t *)(0xa4410000u+(n)))
#define REG8(n) (*(volatile uint8_t *)(0xa4410000u+(n)))
#define SMR REG16(0)
#define BRR REG8(4)
#define SCR REG16(8)
#define FSR REG16(16)
#define FCR REG16(24)
typedef struct {
    tx_feed_t feed;
    sdm_stream_t pcm;
    mp3_source_read_t read;
    void *source;
    const char *title;
    int paused, stop, seek;
    uint32_t seek_to;            /* start position in output frames */
    uint64_t decoded;            /* output bytes produced (from position 0) */
    /* WAV source (NULL fetch: compressed source) */
    pcm_fetch_t wfetch;
    void *wctx;
    uint32_t wcount, wrate, wpos;
    int kbd_hold;                /* gint keyboard running until all keys are up */
    uint32_t kbd_quiet_at;       /* live_pos() when a key was last seen down */
    int opus;                    /* Ogg Opus source (decoded at 24 kHz) */
} live_t;
#define FRAME_BYTES ((uint32_t)SDM_FRAME_BYTES)
static void screen(live_t *s,const char *state)
{
    char vol[24];
    live_volume_label(vol,live_volume);
    uint32_t seconds=(s->seek_to+(tx_feed_consumed(&s->feed)/FRAME_BYTES))/SDM_RATE;
    ui_player_t p={s->title,s->wfetch ? "WAV" : s->opus ? "Opus" : "MP3",state,
        seconds,s->wfetch ? s->wcount/s->wrate : 0,vol,live_wait_name(live_decode_fill),!s->paused};
    ui_player(&p);
}
static int fill_kind;              /* 1 Opus, 2 MP3 filler ready for this stream */
/* Transport keys. gint's keyboard timer is paused while the stream plays
   (its 64/128 Hz scan was heard as clicks) and KEYSC is read directly.
   All six data words are read, in order, every time, exactly as gint's own
   keysc_scan() does: the KEYSC seems to keep an internal buffer that only
   full reads drain (Casiopeia KEYSC thread: "only accurate if accessed
   often enough"), and with partial reads exactly one key change showed up
   after the timer stopped (0.9.1: pause once, then no key at all). The
   reads happen every 64 output frames (2.2 ms), any key going down
   restarts gint's driver, and it is paused again only after 35.6 ms with
   every key up, so its state has caught up with the release. While the
   stream is paused the driver runs and its event queue is used as well;
   OK/EXE from the queue counts only after OK has been seen up while paused
   (the resumed driver reported the still-held OK as a new press). */
#ifndef KEYSC_READ                 /* tests/test_keys.c models the KEYSC */
#define KEYSC_READ(i) (((volatile uint16_t *)0xa44b0000u)[i])
#endif
#ifndef LIVE_PAUSE_DELAY           /* ~5-10 ms; not sleep_ms(), which needs a
                                      free timer and froze the player once */
#define LIVE_PAUSE_DELAY() for(volatile unsigned d=0; d<300000u; d++) {}
#endif
#define KBD_SETTLE_FRAMES 1024u    /* 35.6 ms of output, >= 2 gint scans */
static uint16_t live_words[6];
static unsigned live_scan(void)
{
    unsigned any=0;
    for(int i=0;i<6;i++) { live_words[i]=KEYSC_READ(i); any|=live_words[i]; }
    return any;                    /* nonzero while any key is down */
}
static int live_key(int basic)
{
    unsigned row=(unsigned)basic%10, col=(unsigned)basic/10-1;
    return (live_words[row>>1] >> (col+8*(row&1))) & 1;
}
/* Output position including the DMA chunk in flight (ring.read alone only
   moves when a 32 KiB chunk completes). */
static uint32_t live_pos(live_t *s)
{
    return s->feed.ring.write+tx_feed_free(&s->feed)-TX_RING_SIZE;
}
static unsigned live_keys_prev;
static void seek_by(live_t *s, int right)
{
    uint32_t now=s->seek_to+(tx_feed_consumed(&s->feed)/FRAME_BYTES);
    s->seek_to=!right ? (now<144000 ? 0 : now-144000) :
        (now>UINT32_MAX-144000 ? UINT32_MAX : now+144000);
    s->seek=1;
}
static void kbd_pause(void);
static void kbd_resume(void);
static int kbd_paused;
static int ok_armed;
static void toggle_pause(live_t *s)
{
    if(!s->paused) {
        tx_feed_pause(&s->feed);
        s->paused=1;
        ok_armed=0;
        kbd_resume(); s->kbd_hold=0;
        clearevents();
        live_keys_prev=~0u;     /* the pause key is still held */
        screen(s,"Paused");
    } else {
        s->paused=0;
        screen(s,s->feed.running ? "Playing" : "Buffering...");
        live_keys_prev=~0u;     /* ignore keys held across the resume */
        s->kbd_hold=1;          /* keyboard stays live until OK is released */
        s->kbd_quiet_at=live_pos(s);
        tx_feed_resume(&s->feed);
    }
}
static uint32_t live_keys_at;
static int controls(live_t *s)
{
    key_event_t e;
    do {
        /* At most once per 64 frames of output, or every pass while paused:
           the output loop calls this in a tight spin when the ring is full. */
        uint32_t pos=live_pos(s);
        if(s->paused || pos-live_keys_at>=64u*FRAME_BYTES) {
            live_keys_at=pos;
            unsigned any=live_scan();
            const platform_keys_t *pk=&platform_keys;
            unsigned k=(unsigned)(live_key(pk->ok)|live_key(pk->exe)) | ((unsigned)live_key(pk->left)<<1) |
                ((unsigned)live_key(pk->right)<<2) |
                ((unsigned)(live_key(pk->back)|live_key(pk->ac)|live_key(pk->home)|live_key(pk->tools))<<3);
            unsigned pressed=k & ~live_keys_prev; live_keys_prev=k;
            if(s->paused && !(k&1)) ok_armed=1;
            if(pressed&8) s->stop=1;
            /* While paused the event queue also sees these keys; take
               seeks only from it so one press cannot seek twice. */
            if(!s->paused && (pressed&2)) seek_by(s,0);
            if(!s->paused && (pressed&4)) seek_by(s,1);
            if(pressed&1) { toggle_pause(s); if(!s->paused) break; }
            if(!s->paused) {
                if(any) {
                    if(!s->kbd_hold) { kbd_resume(); s->kbd_hold=1; }
                    s->kbd_quiet_at=pos;
                } else if(s->kbd_hold && pos-s->kbd_quiet_at>=KBD_SETTLE_FRAMES*FRAME_BYTES) {
                    kbd_pause(); s->kbd_hold=0;
                }
            }
        }
        /* gint's event queue only fills while its keyboard driver runs (a key
           down, then 35.6 ms) or while paused; pollevent() is ROM code, which
           the fx-CG50 maps from NOR, so leave it alone the rest of the time. */
        while((s->kbd_hold || s->paused) && (e=pollevent()).type!=KEYEV_NONE) {
            /* + toggles the wait load at any time: gint's driver is resumed
               whenever a key is down, so its queue sees the press while
               playing as well. */
            if(e.type==KEYEV_DOWN && e.key==KEY_ADD) {
                live_decode_fill=(live_decode_fill+1)%3;
                if(s->paused) screen(s,"Paused");
                continue;
            }
            if(e.type==KEYEV_DOWN && (e.key==KEY_UP || e.key==KEY_DOWN)) {
                if(e.key==KEY_UP && live_volume<SDM_VOLUME_TOP) live_volume++;
                if(e.key==KEY_DOWN && live_volume>1) live_volume--;
                sdm_stream_set_volume(&s->pcm,live_volume);
                if(s->paused) screen(s,"Paused");
                continue;
            }
            /* While playing the queue is only drained; while paused, keys
               come from both (whichever sees the press first). */
            if(e.type!=KEYEV_DOWN || !s->paused) continue;
            if(e.key==KEY_BACK || e.key==KEY_TOOLS || e.key==KEY_EXIT) s->stop=1;
            else if(e.key==KEY_LEFT) seek_by(s,0);
            else if(e.key==KEY_RIGHT) seek_by(s,1);
            else if((e.key==KEY_OK || e.key==KEY_EXE) && ok_armed) { toggle_pause(s); break; }
        }
        if(s->stop || s->seek || s->feed.fault) return 1;
        if(s->paused) LIVE_PAUSE_DELAY();
    } while(s->paused);
    return 0;
}
static void start(live_t *s)
{
    if(s->feed.fault || s->stop || s->seek) return;
    screen(s,"Playing");
    tx_feed_start(&s->feed);
}
/* While waiting for ring space the CPU runs 64-bit multiply-adds in the
   cache. The TX high level is the I/O supply, and the supply follows the
   CPU's load, so the wait load matters. Measured (261014_27, 261014_41):
   this load is the closest to what decoding draws (MP3: +-38 Hz sidebands
   -55 dB, against -43.5 dB when polling the SCIF), and its supply hump sits
   at ~14.4 kHz, where it coincides with its mirror about the 28.8 kHz
   frame rate. Heavier I/O loads push the hump above 22 kHz, but the mirror
   (28.8 kHz - f) then lands at 3-5 kHz: 4 dB(A) noisier. */
static int32_t wait_small[256];
static volatile int64_t wait_sink;
int live_decode_fill=LIVE_WAIT_DECODE;   /* wait load (main.c, + key cycles it; live while playing) */
unsigned live_volume=SDM_VOLUME_SAFE;
void live_volume_label(char *out,unsigned volume)
{
    if(volume<=SDM_VOLUME_MAX) sprintf(out,"%u/16",volume);
    else sprintf(out,"16/16 +%u.%u dB",(volume-16)*3/2,((volume-16)*3%2)*5);
}
static uint32_t randk_state=0x9e3779b9u;
static void wait_load(void)
{
    if(live_decode_fill==LIVE_WAIT_DECODE) {    /* checked on every call: + switches it live */
        if(fill_kind==1) { opus_fill_step(); return; }
        if(fill_kind==2) { mp3_fill_step(); return; }
    }
    int64_t acc=0;
    if(live_decode_fill==LIVE_WAIT_RANDOM) {
        /* Random intensity per call (261015_09): the supply hump is smeared
           over about 13-14 kHz instead of standing as a +15..20 dB line, the
           50 Hz family is 5 dB below the plain multiply-add wait, at 1.3 dB(A)
           more hiss. k x 8 cached MACs then (8-k) x 8 idle iterations. */
        randk_state^=randk_state<<13; randk_state^=randk_state>>17; randk_state^=randk_state<<5;
        unsigned k=2u+(randk_state&7u); if(k>8u) k=8u;
        for(unsigned i=0;i<k*8u;i++) acc+=(int64_t)wait_small[i]*wait_small[i+64];
        for(volatile unsigned i=0;i<(8u-k)*8u;i++) {}
        wait_sink+=acc;
        return;
    }
    for(unsigned i=0;i<64;i++) acc+=(int64_t)wait_small[i]*wait_small[i+64];
    wait_sink+=acc;
}
static void wait_init(void)
{
    for(unsigned i=0;i<256;i++) wait_small[i]=(int32_t)(i*40503u+12345u);
}
/* The modulator's sink: one frame code per output frame. The frame is
   generated straight into the ring (tx_ring_put_frame). */
static int output(void *context,unsigned frame)
{
    live_t *s=context;
    /* Frames before seek_to are dropped (the MP3 reader seeks by decoding
       from the start). */
    int skip=s->decoded<(uint64_t)s->seek_to*FRAME_BYTES;
    if(skip) s->decoded+=FRAME_BYTES;
    if(controls(s)) return -1;
    if(skip) return 0;
    uint32_t last=~0u, budget=2000000;   /* progress = live read position */
    while(tx_feed_free(&s->feed)<FRAME_BYTES) {
        /* Full but not started (only possible if the ring did not end
           exactly on its size): start rather than wait forever. */
        if(!s->feed.running) start(s);
        wait_load();
        if(controls(s)) return -1;
        uint32_t now=s->feed.ring.write-(TX_RING_SIZE-tx_feed_free(&s->feed));
        if(now!=last) { last=now; budget=2000000; }
        else if(!--budget) { s->feed.fault=2; return -1; }
    }
    tx_ring_put_frame(&s->feed.ring,frame);
    s->decoded+=FRAME_BYTES;
    tx_feed_kick(&s->feed);
    if(!s->feed.running && tx_ring_used(&s->feed.ring)>=TX_RING_SIZE) start(s);
    return 0;
}
static int read_data(void *context,uint32_t pos,void *data,size_t n)
{
    live_t *s=context;
    return s->read(s->source,pos,data,n);
}
static int emit(void *context,const int16_t *p,size_t n,uint32_t rate)
{
    live_t *s=context;
    if(controls(s)) return -1;
    return sdm_stream_push(&s->pcm,p,n,rate);
}
static int progress(void *context,const decode_stats_t *stats)
{
    (void)stats;
    return controls(context);
}
/* Keyboard scan pause. gint's keysc_set_scan_frequency() cannot be used:
   its source (src/keysc/scan_frequency.c) declares keysc_scan_Hz and
   keysc_tid as int while keysc.c defines them as int16_t, so on this
   big-endian CPU it corrupts the keyboard driver (LTO warns about both).
   Pause gint's keyboard timer itself instead. */
extern int16_t keysc_tid;
static void kbd_pause(void)
{
    if(keysc_tid>=0) { timer_pause(keysc_tid); kbd_paused=1; }
}
static void kbd_resume(void)
{
    if(kbd_paused) { timer_start(keysc_tid); kbd_paused=0; }
}
/* WAV producer: feeds the same SDM/ring/DMA pipeline as the decoders,
   starting at the sample that corresponds to seek_to (no skipping). */
static int wav_produce(live_t *s)
{
    uint32_t i=(uint32_t)((uint64_t)s->seek_to*s->wrate/SDM_RATE);
    s->decoded=(uint64_t)s->seek_to*FRAME_BYTES;
    int16_t buf[256];
    while(i<s->wcount) {
        uint32_t n=s->wcount-i; if(n>256) n=256;
        for(uint32_t j=0;j<n;j++) buf[j]=s->wfetch(s->wctx,i+j);
        if(emit(s,buf,n,s->wrate)) return -1;
        i+=n;
    }
    return 0;
}
static int os_close(void)
{
    return platform_serial_close(1);
}
static int os_open(void)
{
    /* The state entry returns 3 while the OS holds no serial handle and 1
       while one is open. The handle lives in the OS, so a run that ended
       without closing it (crash, power loss, reset) leaves it open for the
       next launch of the add-in; close it and try again before giving up. */
    for(int attempt=0;platform_serial_state()!=3;attempt++) {
        if(attempt>=2) return -1;
        os_close();
    }
    unsigned char mode[6]={0,9,0,0,0,0};
    return platform_serial_open(mode);
}
static int play_session(live_t *s,uint32_t size,unsigned volume,const char **error)
{
    /* 512-bit frames at 28 800 frames/s need the SCIF bit clock Pphi/4 at
       14.7456 Mbit/s, i.e. the doubled peripheral clock that main.c
       switches on around the stream. Anything else would play at the
       wrong speed. */
    if((uint32_t)clock_freq()->Pphi_f/4u!=SDM_RATE*SDM_FRAME_BITS) {
        *error="Peripheral clock is not doubled"; return -1;
    }
    if(gint_world_switch(GINT_CALL(os_open))) {
        *error="Cannot open serial port"; return -1;   /* caller frees s */
    }
    uint16_t smr=SMR,scr=SCR,fcr=FCR;
    uint8_t brr=BRR;
    /* OS 2.00 runs the clock with spread spectrum (SSCGCR.SSEN): a ~9.26 kHz
       tone on the line and the pitch 1/64 low. Off while playing (the same
       SH7305 register on the CG50; gint's CPG driver clears it there too). */
    uint32_t ss=SH7305_CPG.SSCGCR.lword;
    SH7305_CPG.SSCGCR.lword=ss & ~0x80000000u;
    live_keys_prev=~0u;   /* ignore keys already held when playback starts */
    int result=0;
    do {
        s->seek=0; s->paused=0; s->decoded=0;
        /* Keyboard live until every key has been up for a while (the seek
           key may still be held); controls() then pauses it. */
        kbd_resume(); s->kbd_hold=1;
        SCR=0; FCR=6; SMR=0x80; BRR=0;
        for(volatile unsigned i=0;i<10000;i++) {}
        FCR=0; FSR=FSR & (uint16_t)~0x60; SCR=0x20;
        /* Probes the SCIF DMA request first, then falls back to the TMU feeder. */
        if(tx_feed_open(&s->feed)<0) { *error="No DMA/timer for audio stream"; result=-1; break; }
        s->kbd_quiet_at=live_pos(s);
        screen(s,s->seek_to ? "Seeking... BACK cancels" : "Buffering... BACK cancels");
        (void)volume;
        sdm_stream_init(&s->pcm,live_volume,s,output);
        if(!s->wfetch) sdm_stream_set_makeup(&s->pcm,DECODE_MAKEUP_Q12);   /* decoders write 4 dB low */
        decode_stats_t stats={0};
        if(s->wfetch) result=wav_produce(s);
        else if(s->opus) {
            /* The Opus reader seeks itself (granule bisection + pre-roll)
               and emits from the exact start: nothing to skip here. */
            decode_io_t io={s,size,read_data,emit,NULL,progress};
            s->decoded=(uint64_t)s->seek_to*FRAME_BYTES;
            result=opus_file_decode(&io,&stats,(uint32_t)((uint64_t)s->seek_to*5u/144u));
        } else {
            decode_io_t io={s,size,read_data,emit,NULL,progress};
            result=mp3_decode(&io,&stats);
        }
        if(!result && sdm_stream_finish(&s->pcm)) result=-1;
        if(!result) {
            s->feed.eof=1;
            if(!s->feed.running && tx_ring_used(&s->feed.ring)) start(s);
            if(!tx_ring_used(&s->feed.ring) && !s->feed.running) s->feed.complete=1;
            uint32_t last=s->feed.ring.read, budget=2000000;
            while(!s->feed.complete && !s->feed.fault) {
                wait_load();
                if(controls(s)) break;
                uint32_t now=s->feed.ring.read;
                if(now!=last) { last=now; budget=2000000; }
                else if(!--budget) { s->feed.fault=2; break; }
            }
        }
        tx_feed_close(&s->feed);
        if(s->feed.fault) {
            *error=s->feed.fault==1 ? "Stream buffer underrun" : "Serial stream stalled";
            result=-1;
        } else if(!s->stop && !s->seek && result) {
            *error=stats.error ? stats.error : s->wfetch ? "WAV stream failed" :
                s->opus ? "Opus stream failed" : "MP3 stream failed";
            result=-1;
        }
        if(!s->stop && !s->seek && !result) {
            unsigned budget=2000000;
            while(!(FSR&0x40) && budget) budget--;
            if(!budget) { *error="Serial drain timeout"; result=-1; }
        }
        SCR=0; FCR=6;
    } while(s->seek && !s->stop && !s->feed.fault);
    SCR=0; FCR=6; SMR=smr; BRR=brr;
    for(volatile unsigned i=0;i<10000;i++) {}
    FCR=fcr; SCR=scr;
    SH7305_CPG.SSCGCR.lword=ss;
    kbd_resume();
    gint_world_switch(GINT_CALL(os_close));
    if(s->wfetch) {
        uint64_t frames=(uint64_t)s->seek_to+tx_feed_consumed(&s->feed)/FRAME_BYTES;
        uint64_t pos=frames*s->wrate/SDM_RATE;
        s->wpos=pos>s->wcount ? s->wcount : (uint32_t)pos;
        /* Rounding can leave a finished stream a sample short of the end,
           which would make the next OK "resume" at the last sample. */
        if(!result && !s->stop && !*error) s->wpos=s->wcount;
    }
    /* The caller draws the final state (main.c, back on the normal clock). */
    if(s->stop) result=1;
    return result;
}
int mp3_play_stream(mp3_source_read_t read,void *source,uint32_t size,
    const char *title,unsigned volume,const char **error)
{
    live_t *s=calloc(1,sizeof *s);
    if(!s) { *error="Not enough RAM for stream buffer"; return -1; }
    s->read=read; s->source=source; s->title=title;
    wait_init();
    fill_kind=!mp3_fill_init() ? 2 : 0;        /* always ready: + may enable it mid-stream */
    int result=play_session(s,size,volume,error);
    if(fill_kind) mp3_fill_free();
    fill_kind=0;
    free(s);
    return result;
}
int opus_play_stream(mp3_source_read_t read,void *source,uint32_t size,
    const char *title,unsigned volume,const char **error)
{
    live_t *s=calloc(1,sizeof *s);
    if(!s) { *error="Not enough RAM for stream buffer"; return -1; }
    s->read=read; s->source=source; s->title=title; s->opus=1;
    wait_init();
    fill_kind=!opus_fill_init() ? 1 : 0;       /* always ready: + may enable it mid-stream */
    int result=play_session(s,size,volume,error);
    if(fill_kind) opus_fill_free();
    fill_kind=0;
    free(s);
    return result;
}
int wav_play_stream(pcm_fetch_t fetch,void *ctx,uint32_t count,uint32_t rate,
    uint32_t *position,const char *title,unsigned volume,const char **error)
{
    if(rate<4000 || rate>48000) { *error="Unsupported WAV sample rate"; return -1; }
    live_t *s=calloc(1,sizeof *s);
    if(!s) { *error="Not enough RAM for stream buffer"; return -1; }
    s->title=title;
    s->wfetch=fetch; s->wctx=ctx; s->wcount=count; s->wrate=rate;
    uint32_t start=*position<count ? *position : 0;
    s->seek_to=(uint32_t)((uint64_t)start*SDM_RATE/rate);
    wait_init();
    int result=play_session(s,0,volume,error);
    *position=s->wpos;
    free(s);
    return result;
}
