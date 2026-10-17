#pragma once
#include <stdint.h>
enum { BFile_ReadOnly=0, BFile_WriteOnly=1, BFile_ReadWrite=2 };
#define BFile_EntryNotFound (-1)
#define BFile_Type_Directory 0
struct BFile_FileInfo { uint16_t index, type; uint32_t file_size, data_size; uint32_t property, address; };
int BFile_Open(const uint16_t *file,int mode);
int BFile_Close(int fd);
int BFile_Read(int fd,void *data,int size,int whence);
int BFile_Size(int fd);
int BFile_Seek(int fd,int pos);
#define BFile_File 1
int BFile_Remove(const uint16_t *path);
int BFile_Create(const uint16_t *path,int type,int *size);
int BFile_Write(int fd,const void *data,int even_size);
int BFile_FindFirst(const uint16_t *pattern,int *handle,uint16_t *found,struct BFile_FileInfo *info);
int BFile_FindNext(int handle,uint16_t *found,struct BFile_FileInfo *info);
int BFile_FindClose(int handle);
