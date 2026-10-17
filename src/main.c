#include <gint/display.h>
#include <gint/keyboard.h>
#include <gint/gint.h>
#include <gint/bfile.h>
#include <gint/clock.h>
#include <gint/timer.h>
#include <gint/hardware.h>
#include <stdint.h>
#include <stdlib.h>
#include <stdio.h>
#include <string.h>
#include "wav.h"
#include "library.h"
#include "ui.h"
#include "transport.h"
#include "flashmap.h"
#include "mp3-live.h"
#include "sdm.h"
#include "overclock.h"
#include "platform.h"
#include "nor-source.h"

static uint8_t *file_data;
static uint32_t *file_map;
static uint32_t pcm_offset;
#define NOR_SIZE 0x02000000u
#define NOR_BASE ((const uint8_t *)0xa0000000u)
static int stop_requested(void);
static wav_t audio;
static const char *error;
static unsigned file_size;
static transport_t transport;
static unsigned volume=SDM_VOLUME_SAFE;
static void player_screen(int playing, const char *notice);

static int query_wav_size(void)
{
    int fd = BFile_Open(selected_path,BFile_ReadOnly);
    if(fd < 0) { error = "Cannot open selected file"; return -1; }
    int size = BFile_Size(fd);
    BFile_Close(fd);
    if(size < 12) { error = "File too small / size error"; return -1; }
    file_size = (unsigned)size;
    return 0;
}
static int load_wav(void)
{
    int fd=BFile_Open(selected_path,BFile_ReadOnly);
    if(fd<0) { error="Cannot open selected file"; return -1; }
    int size=BFile_Size(fd), result=-1;
    if(size<12) error="File too small / size error";
    else if((unsigned)size!=file_size) error="File changed; retry loading";
    else if(BFile_Read(fd,file_data,size,0)!=size) error="File read error";
    else {
        error=wav_parse(file_data,file_size,&audio);
        result=error ? -1 : 0;
    }
    BFile_Close(fd);
    return result;
}
static int file_read(void *context, uint32_t offset, void *data, size_t size)
{
    return BFile_Read(*(int *)context,data,(int)size,(int)offset)==(int)size ? 0 : -1;
}
static int map_input(int is_wav)
{
    int fd=BFile_Open(selected_path,BFile_ReadOnly);
    if(fd<0) { error="Cannot open selected file"; return -1; }
    int size=BFile_Size(fd), result=-1;
    wav_info_t info={0};
    if(size<12 || (unsigned)size!=file_size) error="File changed; retry loading";
    else if(is_wav) error=wav_parse_reader(file_read,&fd,file_size,&info);
    if(error) goto close;
    uint8_t chunk[FLASHMAP_CHUNK];
    uint32_t expected=0x00c80000u;
    for(uint32_t pos=0,page=0;pos<file_size;pos+=FLASHMAP_CHUNK,page++) {
        size_t n=file_size-pos;
        if(n>FLASHMAP_CHUNK) n=FLASHMAP_CHUNK;
        if(file_read(&fd,pos,chunk,n)) { error="File read error"; goto close; }
        int32_t found=flashmap_find(NOR_BASE,NOR_SIZE,chunk,n,expected,stop_requested);
        if(found<0) {
            error=stop_requested() ? "Loading cancelled" : "File chunk not found in NOR";
            goto close;
        }
        file_map[page]=(uint32_t)found;
        expected=(uint32_t)found+FLASHMAP_CHUNK;
    }
    pcm_offset=info.offset;
    if(is_wav) audio=(wav_t){NULL,info.count,info.rate,info.bits};
    result=0;
close:
    BFile_Close(fd);
    return result;
}
static int key_down(int basic)
{
    volatile uint16_t *keys = (volatile uint16_t *)0xa44b0000;
    unsigned row = (unsigned)basic%10, col = (unsigned)basic/10-1;
    return (keys[row>>1] >> (col+8*(row&1))) & 1;
}
static int stop_requested(void)
{
    platform_keys_t *k=&platform_keys;
    return key_down(k->back) || key_down(k->ac) || key_down(k->home);
}
static void release_controls(void)
{
    /* World switching can present a still-held OS-world key as a new gint
       press. Consume its release before accepting another action. */
    platform_keys_t *k=&platform_keys;
    while(key_down(k->ok) || key_down(k->exe) || key_down(k->left) || key_down(k->right) ||
        stop_requested() || key_down(k->tools)) sleep_ms(10);
    clearevents();
}
static int is_mp3, is_opus;
static const char *idle="Ready";   /* what the player shows between streams */
/* fx-CG100: gint's table of OS 2.00 entry points has no PowerOff (gint
   syscalls.S, CASIOWIN API 2), so getkey()'s SHIFT+AC handler stopped the
   add-in with "Missing syscall for this OS version". There the keys are
   read without it, and SHIFT+AC leaves the add-in instead, back to the
   loader, from where the OS powers off as usual. The fx-CG50 keeps gint's
   power-off. Every getkey() of the player goes through here; the stream
   itself reads the keyboard directly (mp3-live.c, AC stops it). */
static int quit_requested;
static key_event_t player_getkey(void)
{
    if(gint[HWCALC]!=HWCALC_FXCG100) return getkey();
    key_event_t e=getkey_opt(GETKEY_DEFAULT & ~GETKEY_POWEROFF,NULL);
    if(e.type==KEYEV_DOWN && e.key==KEY_ACON && e.shift && !e.alpha) {
        quit_requested=1;
        e.key=KEY_BACK;                    /* leaves whatever screen it is */
    }
    return e;
}
static void player_screen(int playing, const char *notice)
{
    char vol[24], format[32];
    live_volume_label(vol,volume);
    if(is_opus) snprintf(format,sizeof format,"Opus");
    else if(is_mp3) snprintf(format,sizeof format,"MP3");
    else if(audio.rate && audio.count) snprintf(format,sizeof format,"WAV %u-bit %u Hz",audio.bits,audio.rate);
    else snprintf(format,sizeof format,"WAV");
    ui_player_t p={selected_title,format,notice,
        audio.rate ? transport.position/audio.rate : 0,
        audio.rate ? audio.count/audio.rate : 0,
        vol,live_wait_name(live_decode_fill),playing};
    ui_player(&p);
}
/* The cursor survives a trip to the player (the next file is one DOWN
   away) and is reset only when the folder changes. */
static int browse(void)
{
    static int selected, top;
    for(;;) {
        if(quit_requested) return 0;
        int scan=gint_world_switch(GINT_CALL(library_scan));
        if(selected>=library_count) selected=library_count ? library_count-1 : 0;
        if(top>selected) top=selected;
        if(selected>=top+7) top=selected-6;
        for(;;) {
            char directory[46]; library_directory_label(directory,sizeof directory);
            ui_library_title(directory,library_count);
            if(scan<0) dprint(12,60,C_BLACK,"Cannot read this folder. %s: retry",platform_hints->tools);
            else if(!library_count) dtext(12,60,C_BLACK,"No WAV / MP3 / Opus in this folder");
            for(int row=0;row<7 && top+row<library_count;row++) {
                library_entry_t *entry=&library_entries[top+row];
                ui_library_row(row,entry->label,top+row==selected,entry->folder);
            }
            dupdate();
            int key=player_getkey().key;
            if(quit_requested) return 0;
            if(key==KEY_BACK) {
                if(!library_parent()) return 0;
                selected=top=0; break;
            }
            if(key==KEY_TOOLS) { selected=top=0; break; }
            if(key==KEY_UP && selected>0) selected--;
            if(key==KEY_DOWN && selected+1<library_count) selected++;
            /* The fx-CG50 has no PAGE keys: LEFT / RIGHT page on both. */
            if(key==KEY_PAGEUP || key==KEY_LEFT) selected=selected>7 ? selected-7 : 0;
            if((key==KEY_PAGEDOWN || key==KEY_RIGHT) && library_count)
                selected=selected+7<library_count ? selected+7 : library_count-1;
            if(selected<top) top=selected;
            if(selected>=top+7) top=selected-6;
            if((key==KEY_EXE || key==KEY_OK) && library_count) {
                int choice=library_choose(selected);
                if(choice==0) return 1;
                if(choice==1) { selected=top=0; break; }
            }
        }
    }
}
/* The decoders and the stream run from .data (CMakeLists.txt). gint copied
   .data in through the operand cache, and instruction fetches read SDRAM:
   write it back and drop any I-cache line for these addresses before the
   first call into it. */
static void ram_code_sync(void)
{
#if defined(__sh__)
    extern char rdata, sdata;
    uintptr_t end=(uintptr_t)&rdata+(uintptr_t)&sdata;
    for(uintptr_t a=(uintptr_t)&rdata & ~(uintptr_t)31;a<end;a+=32) {
        register uintptr_t line __asm__("r4")=a;
        /* ocbwb @r4; icbi @r4 (SH-4A, written as a word for sh3eb-elf as) */
        __asm__ volatile("ocbwb @%0\n\t.word 0x04e3"::"r"(line):"memory");
    }
#endif
}
int main(void)
{
    ram_code_sync();
    /* fx-CG50: after main() returns the OS keeps the add-in as the current
       one, and selecting it again only resumes the finished context, which
       exits at once (a white flash, back to the menu) until another app has
       run. gint instead opens the main menu itself and restarts main() when
       the add-in is selected again. No-op on the fx-CG100 (back to MPM). */
    gint_setrestart(1);
    const char *unsupported=platform_init();
    if(unsupported) {
        ui_message(unsupported,"Needs fx-CG50 or fx-CG100 OS 2.00");
        player_getkey(); return 1;
    }
    player_overclock_init();
    cpg_compute_freq();
    /* The stream doubles this clock (overclock.c); both rates assume the
       F5 profile's 29.4912 MHz here. */
    if((unsigned)clock_freq()->Pphi_f!=29491200u) {
        ui_message("Unsupported peripheral clock","Expected 29.4912 MHz (Ptune F5 profile)");
        player_getkey(); return 1;
    }
    while(browse()) {
        error=NULL; audio=(wav_t){0}; transport=(transport_t){0};
        size_t path_len=0;
        while(selected_path[path_len]) path_len++;
        is_mp3=path_len>=4 && selected_path[path_len-4]=='.' &&
            (selected_path[path_len-3]=='m' || selected_path[path_len-3]=='M') &&
            (selected_path[path_len-2]=='p' || selected_path[path_len-2]=='P') &&
            selected_path[path_len-1]=='3';
        is_opus=path_len>=5 && selected_path[path_len-5]=='.' &&
            (selected_path[path_len-4]|0x20)=='o' && (selected_path[path_len-3]|0x20)=='p' &&
            (selected_path[path_len-2]|0x20)=='u' && (selected_path[path_len-1]|0x20)=='s';
        int loaded;
        if(is_mp3 || is_opus) {
            player_screen(0,is_opus ? "Indexing Opus... BACK cancels" : "Indexing MP3... BACK cancels");
            if(gint_world_switch(GINT_CALL(query_wav_size))<0) {
                player_screen(0,error); player_getkey(); continue;
            }
            if(file_size>NOR_SIZE) {
                player_screen(0,"File exceeds physical NOR capacity"); player_getkey(); continue;
            }
            size_t pages=(file_size+FLASHMAP_CHUNK-1)/FLASHMAP_CHUNK;
            file_map=malloc(pages*sizeof *file_map);
            if(!file_map) {
                player_screen(0,"Not enough RAM for file index"); player_getkey(); continue;
            }
            loaded=gint_world_switch(GINT_CALL(map_input,0));
            audio=(wav_t){NULL,0,SDM_RATE,16};
        } else {
            player_screen(0,"Loading WAV...");
            if(gint_world_switch(GINT_CALL(query_wav_size))<0) {
                player_screen(0,error); player_getkey(); continue;
            }
            file_data=malloc(file_size);
            if(!file_data) {
                if(file_size>NOR_SIZE) {
                    player_screen(0,"WAV exceeds physical NOR capacity"); player_getkey(); continue;
                }
                size_t pages=(file_size+FLASHMAP_CHUNK-1)/FLASHMAP_CHUNK;
                file_map=malloc(pages*sizeof *file_map);
                if(!file_map) {
                    player_screen(0,"Not enough RAM for small file index"); player_getkey(); continue;
                }
                player_screen(0,"Indexing large WAV... BACK cancels");
            }
            loaded=file_data ? gint_world_switch(GINT_CALL(load_wav)) :
                gint_world_switch(GINT_CALL(map_input,1));
        }
        if(loaded<0) {
            release_controls();
            player_screen(0,error); player_getkey();
            free(file_data); file_data=NULL;
            free(file_map); file_map=NULL; continue;
        }
        idle="Ready"; player_screen(0,idle);
        for(;;) {
            int key=player_getkey().key;
            if(key==KEY_BACK || key==KEY_TOOLS) break;
            if(key==KEY_UP && volume<SDM_VOLUME_TOP) { volume++; player_screen(0,idle); }
            if(key==KEY_DOWN && volume>1) { volume--; player_screen(0,idle); }
            if(key==KEY_ADD) {
                live_decode_fill=(live_decode_fill+1)%3;
                player_screen(0,live_decode_fill==LIVE_WAIT_DECODE ? "Wait: decode (default, no hum)" :
                    live_decode_fill==LIVE_WAIT_RANDOM ? "Wait: random (smeared hump)" : "Wait: multiply-add (least hiss)");
            }
            if((key==KEY_LEFT || key==KEY_RIGHT) && audio.count) {
                /* WAV only: the decoders start every stream at 0 and seek
                   while playing. */
                transport_seek(&transport,audio.count,audio.rate,key==KEY_LEFT ? -5 : 5);
                idle=transport.position ? "Paused" : "Ready"; player_screen(0,idle);
            }
            if(key==KEY_EXE || key==KEY_OK) {
                /* The stream runs on the doubled peripheral clock with
                   512-bit frames (sdm.h, overclock.c); the UI stays on the
                   normal one. gint re-applies the current setting after
                   each world switch (the serial open/close in the stream). */
                player_overclock_pphi_x2(1);
                release_controls(); error=NULL;
                live_volume=volume;
                int result;
                /* The stream reads the file through RAM-resident code (nor-source.c). */
                nor_source_t source={NOR_BASE,file_map,file_size,audio.samples,pcm_offset,audio.bits};
                if(is_mp3 || is_opus) {
                    result=is_opus ?
                        opus_play_stream(nor_source_read,&source,file_size,selected_title,volume,&error) :
                        mp3_play_stream(nor_source_read,&source,file_size,selected_title,volume,&error);
                } else {
                    if(transport.position>=audio.count) transport=(transport_t){0};
                    uint32_t pos=transport.position;
                    result=wav_play_stream(nor_source_sample,&source,audio.count,audio.rate,
                        &pos,selected_title,volume,&error);
                    transport.position=pos; transport.phase=0;
                }
                player_overclock_pphi_x2(0);
                release_controls();
                volume=live_volume;               /* UP/DOWN during the stream */
                if(result==1) break;              /* BACK: back to the list */
                idle=error ? error : "Finished"; player_screen(0,idle);
            }
        }
        free(file_data); file_data=NULL;
        free(file_map); file_map=NULL;
    }
    /* BACK at the root: nothing is open or doubled here (every stream
       closes the serial port and restores Pphi before returning); gint
       puts the OS peripherals back when main() returns. */
    clearevents();
    return 0;
}
