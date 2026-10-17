/* Host test of the stream transport keys (controls() / toggle_pause() in
   src/mp3-live.c) against models of the KEYSC data registers and of gint's
   keyboard timer. Scripted presses with random lengths and timing; checks
   that every OK/EXE toggles play/pause, LEFT seeks, BACK stops, and that
   the keyboard timer is paused for most of the playing time.

   KEYSC models (the real behaviour is not documented; see mp3-live.c):
     0 live      data registers always show the current keys
     1 queue     changes queue up; the oldest unread one is what the data
                 registers show; reading all six words (0..5) drops it
     2 gint-only like 1, but only gint's own scan drops entries
   The 0.9.1 code (partial KEYSC reads, qa/pre-0.9.2/mp3-live.c) failed
   20-100 % of the trials under the queue models; this is the regression
   test for the 0.9.2 fix. */
#include <stdint.h>
#include <stdio.h>
#include <string.h>
#include <stdlib.h>
static uint16_t mock_read(int i);
static void mock_wait(void);
#define KEYSC_READ(i) mock_read(i)
#define LIVE_PAUSE_DELAY() mock_wait()
#include LIVE_SRC

/* ---- simulated world ---- */
static int model;
static double t_us;                       /* simulated time */
static double pos;                        /* output bytes, live */
static double read_base;                  /* ring.read at last start/resume */
static int playing;                       /* DMA running and not paused */
static uint16_t phys[6];
static uint16_t q[32][6]; static int qn;
static unsigned readmask; static int reader_gint;
static uint16_t mock_read(int i)
{
    uint16_t v=qn ? q[0][i] : phys[i];
    readmask|=1u<<i;
    if(i==5) {
        if(readmask==0x3f && qn && (model==1 || (model==2 && reader_gint))) {
            memmove(q,q+1,sizeof q[0]*(size_t)(qn-1)); qn--;
        }
        readmask=0;
    }
    return v;
}
/* fx-CG100 positions (src/platform.c keys_cg100); mp3-live.c reads them
   from platform_keys. */
platform_keys_t platform_keys={48,31,58,38,68,34,69,27};
/* keys used by the script: basic codes as in live_key() */
typedef struct { int basic, key; } tkey_t;
static const tkey_t tkeys[]={{48,KEY_OK},{31,KEY_EXE},{58,KEY_LEFT},{38,KEY_RIGHT},{68,KEY_BACK},{11,KEY_7}};
enum { T_OK, T_EXE, T_LEFT, T_RIGHT, T_BACK, T_OTHER };
static void key_pos(int basic,int *word,int *bit)
{
    int row=basic%10, col=basic/10-1;
    *word=row>>1; *bit=col+8*(row&1);
}
static void set_key(int k,int down)
{
    int w,b; key_pos(tkeys[k].basic,&w,&b);
    uint16_t old=phys[w];
    if(down) phys[w]|=(uint16_t)(1u<<b); else phys[w]&=(uint16_t)~(1u<<b);
    if(model && phys[w]!=old && qn<32) memcpy(q[qn++],phys,sizeof phys);
}
/* gint keyboard driver */
int16_t keysc_tid=3;
static int timer_running=1; static double next_tick, tick_us=7812.5;  /* 128 or 64 Hz */
static uint16_t gstate[6];
static key_event_t evq[256]; static int evh,evn;
static double timer_on_us;                /* playing time with the timer on */
void timer_start(int id) { if(id==keysc_tid && !timer_running) { timer_running=1; next_tick=t_us+tick_us; } }
void timer_pause(int id) { if(id==keysc_tid) timer_running=0; }
static void gint_tick(void)
{
    uint16_t w[6];
    reader_gint=1; for(int i=0;i<6;i++) w[i]=mock_read(i); reader_gint=0;
    for(unsigned k=0;k<sizeof tkeys/sizeof *tkeys;k++) {
        int wd,b; key_pos(tkeys[k].basic,&wd,&b);
        int was=(gstate[wd]>>b)&1, is=(w[wd]>>b)&1;
        if(was!=is && evn<256) { evq[(evh+evn++)%256]=(key_event_t){is ? KEYEV_DOWN : KEYEV_UP,tkeys[k].key}; }
    }
    memcpy(gstate,w,sizeof w);
}
key_event_t pollevent(void)
{
    if(!evn) return (key_event_t){KEYEV_NONE,0};
    key_event_t e=evq[evh]; evh=(evh+1)%256; evn--; return e;
}
void clearevents(void) { evn=0; }
/* script and the checks made while time runs (also inside controls(),
   which does not return while paused) */
typedef struct { double t; int key, down; } step_t;
static step_t script[64]; static int nscript, iscript;
static live_t *cur;
static int expect_paused[16]; static double check_t[16]; static int nchk, ichk, check_fail;
static void advance(double dt)
{
    double end=t_us+dt;
    while(t_us<end) {
        double step=10.0; t_us+=step;
        while(iscript<nscript && script[iscript].t<=t_us) { set_key(script[iscript].key,script[iscript].down); iscript++; }
        if(timer_running && t_us>=next_tick) { gint_tick(); next_tick+=tick_us; }
        if(playing) { pos+=(double)SDM_FRAME_BYTES*step/34.7222; if(timer_running) timer_on_us+=step; }
        while(ichk<nchk && t_us>=check_t[ichk]) {
            if(!cur || cur->paused!=expect_paused[ichk]) check_fail=1;
            ichk++;
        }
    }
}
static void mock_wait(void) { advance(6000); }
/* feed and display mocks */
uint32_t tx_feed_free(const tx_feed_t *f) { return TX_RING_SIZE-(f->ring.write-(uint32_t)pos); }
uint32_t tx_feed_consumed(const tx_feed_t *f)
{
    (void)f; if(!playing) return (uint32_t)read_base;
    return (uint32_t)(read_base+32768.0*(double)(int)((pos-read_base)/32768.0));
}
void tx_feed_pause(tx_feed_t *f) { f->paused=1; playing=0; read_base=pos; }
void tx_feed_resume(tx_feed_t *f) { f->paused=0; playing=1; read_base=pos; }
void dclear(int c) { (void)c; }
void dtext(int x,int y,int c,const char *s) { (void)x;(void)y;(void)c;(void)s; }
void dprint(int x,int y,int c,const char *f,...) { (void)x;(void)y;(void)c;(void)f; }
void dupdate(void) {}
void ui_player(const ui_player_t *p) { (void)p; }

/* ---- one trial ---- */
static double urand(double a,double b) { return a+(b-a)*(rand()/(double)RAND_MAX); }
static void session_start(live_t *s)
{
    /* what play_session does at the top of its loop */
    s->seek=0; s->paused=0;
    kbd_resume(); s->kbd_hold=1; s->kbd_quiet_at=live_pos(s);
    playing=1; read_base=pos;
}
typedef struct { int fail; double timer_frac; } result_t;
static result_t trial(int m,int with_seek)
{
    model=m; t_us=0; pos=0; read_base=0; playing=1; memset(phys,0,sizeof phys); qn=0; readmask=0;
    timer_running=1; next_tick=urand(0,tick_us); memset(gstate,0,sizeof gstate); evh=evn=0; timer_on_us=0;
    nscript=iscript=0;
    /* expected state after each press */
    nchk=ichk=check_fail=0; cur=NULL; int expect_seek_at=-1;
    double t=600e3; int paused=0;
    for(int n=0;n<6;n++) {
        if(with_seek && n==2) {           /* LEFT while playing, then another key */
            double len=urand(40e3,300e3);
            script[nscript++]=(step_t){t,T_LEFT,1}; script[nscript++]=(step_t){t+len,T_LEFT,0};
            expect_seek_at=nscript-2; t+=urand(500e3,900e3);
            script[nscript++]=(step_t){t,T_OTHER,1}; script[nscript++]=(step_t){t+urand(40e3,300e3),T_OTHER,0};
            t+=urand(500e3,900e3);
        }
        int k=(rand()&1) ? T_OK : T_EXE;
        double len=urand(40e3,400e3);
        script[nscript++]=(step_t){t,k,1}; script[nscript++]=(step_t){t+len,k,0};
        paused=!paused; check_t[nchk]=t+len+150e3; expect_paused[nchk++]=paused;
        t+=len+urand(350e3,900e3);
    }
    script[nscript++]=(step_t){t,T_BACK,1}; script[nscript++]=(step_t){t+urand(40e3,300e3),T_BACK,0};
    double t_back=t;
    live_t *s=calloc(1,sizeof *s); s->feed.running=1; s->title="t";
    s->feed.backend=TX_FEED_DMA;
    live_keys_prev=~0u;
    cur=s;
    session_start(s);
    int seeks=0, fail=0; double stop_t=-1;
    while(t_us<t+3e6) {
        s->feed.ring.write=(uint32_t)pos+40000u;
        int r=controls(s);
        if(r) {
            if(s->stop) { stop_t=t_us; break; }
            if(s->seek) { seeks++; session_start(s); }
        }
        advance(100);
    }
    if(check_fail || ichk!=nchk) fail=1;
    if(stop_t<t_back) fail=1;
    if(with_seek && expect_seek_at>=0 && seeks!=1) fail=1;
    result_t r={fail,timer_on_us/(t_us>0 ? t_us : 1)};
    cur=NULL; free(s);
    return r;
}
int main(int argc,char **argv)
{
    int trials=argc>1 ? atoi(argv[1]) : 300;
    int bad=0;
    srand(12345);
    const char *names[]={"live","queue","gint-only"};
    for(int hz=128;hz>=64;hz/=2)
    for(int m=0;m<3;m++) for(int seek=0;seek<2;seek++) {
        tick_us=1e6/hz;
        int fails=0; double frac=0;
        for(int i=0;i<trials;i++) { result_t r=trial(m,seek); fails+=r.fail; frac+=r.timer_frac; }
        printf("%3d Hz scan  %-10s %-6s failed %3d / %d   keyboard timer on %4.1f %% of the time\n",
            hz, names[m], seek ? "+seek" : "", fails, trials, 100*frac/trials);
        bad+=fails;
    }
    return bad ? 1 : 0;
}
