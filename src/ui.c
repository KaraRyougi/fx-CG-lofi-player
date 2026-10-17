#include "ui.h"
#include "platform.h"
#include <gint/display.h>
#include <stddef.h>
#define INK C_RGB(4,4,4)
#define DIM C_RGB(14,14,14)
#define HINT_X2 206
static void header(const char *right)
{
    dclear(C_WHITE);
    dtext(12,12,INK,UI_APP_NAME " " UI_APP_VERSION);
    if(right) dtext_opt(DWIDTH-12,12,DIM,C_NONE,DTEXT_RIGHT,DTEXT_TOP,right,-1);
}
void ui_player(const ui_player_t *p)
{
    header(p->format);
    dtext(12,38,INK,p->title ? p->title : "");
    dtext(12,66,INK,p->state ? p->state : "");
    unsigned pos=p->position, dur=p->duration;
    if(dur) {
        if(pos>dur) pos=dur;
        dprint(12,92,INK,"%02u:%02u / %02u:%02u",pos/60,pos%60,dur/60,dur%60);
        int width=DWIDTH-24, done=(int)((uint64_t)width*pos/dur);
        drect(12,108,DWIDTH-13,113,DIM);
        if(done>0) drect(12,108,12+done-1,113,INK);
    } else dprint(12,92,INK,"%02u:%02u",pos/60,pos%60);
    dprint(12,122,INK,"Volume %s",p->volume ? p->volume : "");
    dprint(12,140,INK,"Wait load: %s",p->wait_load ? p->wait_load : "");
    const platform_hints_t *h=platform_hints;
    dprint(12,166,DIM,p->playing ? "%s: pause" : "%s: play",h->play);
    dtext(HINT_X2,166,DIM,"LEFT / RIGHT: seek 5 s");
    dtext(12,184,DIM,"UP / DOWN: volume");
    dtext(HINT_X2,184,DIM,"+: wait load");
    dprint(12,202,DIM,p->playing ? "%s: stop" : "%s: file list",h->stop);
    dupdate();
}
void ui_message(const char *line1, const char *line2)
{
    header(NULL);
    dtext(12,66,INK,line1 ? line1 : "");
    dtext(12,92,INK,line2 ? line2 : "");
    dtext(12,202,DIM,"Any key: exit");
    dupdate();
}
void ui_library_title(const char *directory, int total)
{
    header(NULL);
    dprint_opt(DWIDTH-12,12,DIM,C_NONE,DTEXT_RIGHT,DTEXT_TOP,"%d %s",total,total==1 ? "file" : "files");
    dtext(12,29,DIM,directory);
    dtext(12,204,DIM,platform_hints->browse);
}
void ui_library_row(int row, const char *name, int selected, int folder)
{
    int y=51+row*21;
    if(selected) drect(8,y-3,DWIDTH-9,y+15,INK);
    dtext(16,y,selected ? C_WHITE : INK,folder ? ">" : " ");
    dtext(31,y,selected ? C_WHITE : INK,name);
}
