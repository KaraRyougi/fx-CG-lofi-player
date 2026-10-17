#ifndef LOFI100_PLATFORM_H
#define LOFI100_PLATFORM_H
#include <stdint.h>
/* The fx-CG100 / Graph Math+ and the fx-CG50 / Graph 90+E are the same
   hardware (SH7305, SCIF, DMAC, CPG/BSC, 32 MiB NOR at 0xa0000000, the
   same clock profile). What differs is software and the key layout: how
   the OS serial port is opened (fixed OS 2.00 entries / syscalls), the
   KEYSC positions of the transport keys and the key names in the hints. */
#define PLATFORM_CG100 1
#define PLATFORM_CG50 2
/* KEYSC positions as "basic" codes: 10*(column+1)+row, read from the
   six KEYSC data words as keys[row>>1] >> (column+8*(row&1)). */
typedef struct {
    uint8_t ok, exe, left, right, back, ac, home, tools;
} platform_keys_t;
typedef struct {
    const char *play;       /* "OK / EXE" */
    const char *stop;       /* "BACK / TOOLS" */
    const char *tools;      /* "TOOLS" */
    const char *browse;     /* file list key line */
} platform_hints_t;
extern int platform;
extern platform_keys_t platform_keys;
extern const platform_hints_t *platform_hints;
/* Detects the model and checks the OS entries the stream needs; NULL on
   success, otherwise a message for the user. Call first in main(). */
const char *platform_init(void);
/* OS serial port, called in the OS world (gint_world_switch). */
int platform_serial_state(void);         /* 3 closed, 1 open */
int platform_serial_open(unsigned char mode[6]);
int platform_serial_close(int abort);
#endif
