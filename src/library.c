#include "library.h"
#include <gint/bfile.h>
#include <string.h>

library_entry_t library_entries[LIBRARY_MAX];
int library_count;
uint16_t library_directory[260]=u"\\\\fls0";
uint16_t selected_path[520];
char selected_title[50];
static unsigned length(const uint16_t *s)
{
    unsigned n=0; while(s[n]) n++; return n;
}
static unsigned lower(unsigned c)
{
    return c>='A' && c<='Z' ? c+32 : c;
}
static void label(const uint16_t *name, char *out, unsigned capacity)
{
    unsigned n=length(name), take=n<capacity-1 ? n : capacity-1;
    for(unsigned i=0;i<take;i++) out[i]=name[i]>=32 && name[i]<127 ? (char)name[i] : '?';
    out[take]=0;
    if(n>=capacity && capacity>4) memcpy(out+capacity-4,"...",4);
}
void library_directory_label(char *out, unsigned capacity)
{
    label(library_directory,out,capacity);
}
static int compare(const library_entry_t *a, const library_entry_t *b)
{
    if(a->folder!=b->folder) return b->folder-a->folder;
    unsigned i=0;
    while(a->name[i] && lower(a->name[i])==lower(b->name[i])) i++;
    return (int)lower(a->name[i])-(int)lower(b->name[i]);
}
int library_scan(void)
{
    uint16_t pattern[264], found[520];
    unsigned n=length(library_directory);
    memcpy(pattern,library_directory,n*2);
    pattern[n++]='\\'; pattern[n++]='*'; pattern[n]=0;
    int handle;
    struct BFile_FileInfo info;
    library_count=0;
    int result=BFile_FindFirst(pattern,&handle,found,&info);
    if(result<0) return result==BFile_EntryNotFound ? 0 : result;
    do {
        /* Some BFile implementations return a basename, some a full path. */
        uint16_t *name=found;
        for(uint16_t *p=found;*p;p++) if(*p=='\\' || *p=='/') name=p+1;
        unsigned len=length(name);
        if(!len || len>=NAME_MAX_UNITS || name[0]=='.') continue;
        int folder=info.type==BFile_Type_Directory;
        int wav=len>=4 && name[len-4]=='.' && lower(name[len-3])=='w' &&
            lower(name[len-2])=='a' && lower(name[len-1])=='v';
        int mp3=len>=4 && name[len-4]=='.' && lower(name[len-3])=='m' &&
            lower(name[len-2])=='p' && name[len-1]=='3';
        int opus=len>=5 && name[len-5]=='.' && lower(name[len-4])=='o' &&
            lower(name[len-3])=='p' && lower(name[len-2])=='u' && lower(name[len-1])=='s';
        if(!folder && !wav && !mp3 && !opus) continue;
        library_entry_t *entry=&library_entries[library_count++];
        memcpy(entry->name,name,(len+1)*2);
        label(name,entry->label,sizeof entry->label);
        entry->folder=folder;
        if(library_count==LIBRARY_MAX) break;
    } while(BFile_FindNext(handle,found,&info)>=0);
    BFile_FindClose(handle);
    for(int i=1;i<library_count;i++) {
        library_entry_t item=library_entries[i]; int j=i;
        while(j && compare(&item,&library_entries[j-1])<0) {
            library_entries[j]=library_entries[j-1]; j--;
        }
        library_entries[j]=item;
    }
    return 0;
}
int library_choose(int index)
{
    if(index<0 || index>=library_count) return -1;
    const library_entry_t *entry=&library_entries[index];
    unsigned dir=length(library_directory), name=length(entry->name);
    if(dir+name+2>=260) return -1;
    memcpy(selected_path,library_directory,dir*2);
    selected_path[dir++]='\\';
    memcpy(selected_path+dir,entry->name,(name+1)*2);
    if(entry->folder) {
        memcpy(library_directory,selected_path,(dir+name+1)*2);
        return 1;
    }
    memcpy(selected_title,entry->label,sizeof selected_title);
    return 0;
}
int library_parent(void)
{
    unsigned len=length(library_directory);
    if(len<=6) return 0;
    while(len>6 && library_directory[len-1]!='\\') len--;
    library_directory[len-1]=0;
    return 1;
}
