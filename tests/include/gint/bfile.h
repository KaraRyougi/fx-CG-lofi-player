/* Host-test declarations matching the gint BFile interface used by library.c. */
#ifndef TEST_BFILE_H
#define TEST_BFILE_H
#include <stdint.h>
#define BFile_EntryNotFound -1
#define BFile_Type_Directory 0
struct BFile_FileInfo {
    uint16_t index,type;
    uint32_t file_size,data_size,property;
    void *address;
};
int BFile_FindFirst(const uint16_t *,int *,uint16_t *,struct BFile_FileInfo *);
int BFile_FindNext(int,uint16_t *,struct BFile_FileInfo *);
int BFile_FindClose(int);
#endif
