#ifndef LOFI100_TRANSPORT_H
#define LOFI100_TRANSPORT_H
#include <stdint.h>
typedef struct { uint32_t position, phase; } transport_t;
void transport_seek(transport_t *t, uint32_t count, uint32_t rate, int seconds);
#endif
