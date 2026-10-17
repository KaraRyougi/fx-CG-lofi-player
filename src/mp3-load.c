#include "mp3-load.h"
#include "mp3-decode.h"
#include "pcm-stream.h"
#include <gint/bfile.h>
#include <gint/gint.h>
#include <gint/keyboard.h>
#include <stdlib.h>
typedef struct {
    const uint16_t *path;
    int fd, output_failed;
    unsigned size;
    pcm_stream_t pcm;
} load_t;
static int os_open(load_t *c)
{
    c->fd=BFile_Open(c->path,BFile_ReadOnly);
    if(c->fd<0) return -1;
    int n=BFile_Size(c->fd);
    if(n<4) { BFile_Close(c->fd); c->fd=-1; return -1; }
    c->size=(unsigned)n;
    return 0;
}
static int os_close(load_t *c) { return BFile_Close(c->fd); }
static int os_read(load_t *c, uint32_t offset, void *data, int n)
{
    return BFile_Read(c->fd,data,n,(int)offset)==n ? 0 : -1;
}
static int read_data(void *context, uint32_t offset, void *data, size_t n)
{
    return gint_world_switch(GINT_CALL(os_read,context,offset,data,(int)n));
}
static int emit(void *context, const int16_t *pcm, size_t n, uint32_t rate)
{
    load_t *c=context;
    if(pcm_stream_push(&c->pcm,pcm,n,rate)) { c->output_failed=1; return -1; }
    return 0;
}
static int progress(void *context, const decode_stats_t *stats)
{
    (void)context; (void)stats;
    key_event_t e;
    while((e=pollevent()).type!=KEYEV_NONE)
        if(e.type==KEYEV_DOWN && (e.key==KEY_BACK || e.key==KEY_TOOLS)) return 1;
    return 0;
}
int mp3_load_ram(const uint16_t *path, pcm_cache_t *cache, const char **error)
{
    load_t *c=calloc(1,sizeof *c);
    if(!c) { *error="Not enough RAM for MP3 loader"; return -1; }
    c->path=path; c->fd=-1;
    if(gint_world_switch(GINT_CALL(os_open,(void *)c))) {
        *error="Cannot open MP3"; free(c); return -1;
    }
    pcm_stream_init(&c->pcm,cache,pcm_cache_write);
    decode_io_t io={c,c->size,read_data,emit,NULL,progress};
    decode_stats_t stats;
    int result=mp3_decode(&io,&stats);
    if(result==0 && pcm_stream_finish(&c->pcm)) c->output_failed=1;
    if(c->output_failed) { *error="Not enough RAM for decoded MP3"; result=-1; }
    else if(result==1) *error="MP3 loading cancelled";
    else if(result<0) *error=stats.error ? stats.error : "MP3 decode failed";
    else if(!cache->size) { *error="MP3 has no audio"; result=-1; }
    gint_world_switch(GINT_CALL(os_close,(void *)c));
    free(c);
    if(result) pcm_cache_free(cache);
    return result ? -1 : 0;
}
