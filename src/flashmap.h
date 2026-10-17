#ifndef LOFI100_FLASHMAP_H
#define LOFI100_FLASHMAP_H
#include <stdint.h>
#include <stddef.h>
#define FLASHMAP_CHUNK 512u
/* Locate an exact logical file chunk in read-only NOR; search from the
   expected next offset, wrapping once. Returns -1 if absent/cancelled. */
int32_t flashmap_find(const uint8_t *nor, uint32_t nor_size,
    const uint8_t *data, size_t size, uint32_t expected, int (*cancel)(void));
#endif
