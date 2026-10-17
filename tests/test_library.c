#include "../src/library.h"
#include <gint/bfile.h>
#include <assert.h>
#include <stdio.h>
#include <string.h>
static int cursor,child,closes,opens;
static int equal(const uint16_t *a,const uint16_t *b)
{
    while(*a && *a==*b) { a++; b++; } return *a==*b;
}
static int fill(uint16_t *name,struct BFile_FileInfo *info)
{
    static const uint16_t *names[]={u"Track2.WAV",u"notes.txt",u"Music",u"b.wav",u"曲.wav",u"sample.Mp3",u"音楽.MP3"};
    const uint16_t *s;
    if(child) { if(cursor) return -16; s=u"Intro.wav"; }
    else { if(cursor==7) return -16; s=names[cursor]; }
    unsigned n=0; do { name[n]=s[n]; } while(s[n++]);
    memset(info,0,sizeof *info);
    info->type=!child && cursor==2 ? 0 : 1;
    cursor++;
    return 0;
}
int BFile_FindFirst(const uint16_t *pattern,int *h,uint16_t *name,struct BFile_FileInfo *info)
{
    child=equal(pattern,u"\\\\fls0\\Music\\*");
    assert(child || equal(pattern,u"\\\\fls0\\*"));
    *h=7; cursor=0; opens++; return fill(name,info);
}
int BFile_FindNext(int h,uint16_t *name,struct BFile_FileInfo *info)
{
    assert(h==7); return fill(name,info);
}
int BFile_FindClose(int h) { assert(h==7); closes++; return 0; }
int main(void)
{
    assert(!library_scan()); assert(library_count==6);
    assert(!strcmp(library_entries[0].label,"Music"));
    assert(!strcmp(library_entries[1].label,"b.wav"));
    assert(!strcmp(library_entries[2].label,"sample.Mp3"));
    assert(!strcmp(library_entries[3].label,"Track2.WAV"));
    assert(!strcmp(library_entries[4].label,"?.wav"));
    assert(library_choose(0)==1);
    assert(equal(library_directory,u"\\\\fls0\\Music"));
    assert(!library_scan() && library_count==1);
    assert(library_choose(0)==0);
    assert(equal(selected_path,u"\\\\fls0\\Music\\Intro.wav"));
    assert(library_parent()==1 && library_parent()==0);
    assert(!library_scan()); assert(!library_choose(4));
    assert(equal(selected_path,u"\\\\fls0\\曲.wav"));
    assert(!library_choose(5));
    assert(equal(selected_path,u"\\\\fls0\\音楽.MP3"));
    assert(!strcmp(selected_title,"??.MP3"));
    assert(library_choose(-1)==-1 && library_choose(library_count)==-1);
    assert(opens==closes);
    puts("WAV/MP3 filtering/sorting, folders, exact Unicode paths and search-handle cleanup passed");
}
