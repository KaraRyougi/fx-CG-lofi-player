#ifndef LOFI100_LIBRARY_H
#define LOFI100_LIBRARY_H
#include <stdint.h>
#define LIBRARY_MAX 96
#define NAME_MAX_UNITS 128
typedef struct {
    uint16_t name[NAME_MAX_UNITS];
    char label[50];
    int folder;
} library_entry_t;
extern library_entry_t library_entries[LIBRARY_MAX];
extern int library_count;
extern uint16_t library_directory[260];
extern uint16_t selected_path[520];
extern char selected_title[50];
int library_scan(void);
int library_choose(int index); /* 1=folder opened, 0=file selected, -1=error */
int library_parent(void); /* 0 at root, 1 if moved up */
void library_directory_label(char *out, unsigned capacity);
#endif
