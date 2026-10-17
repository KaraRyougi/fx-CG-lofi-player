#ifndef LOFI100_NOR_SOURCE_H
#define LOFI100_NOR_SOURCE_H
#include <stddef.h>
#include <stdint.h>
/* The played file as the stream reads it: in NOR through the flashmap
   chunk table (MP3, Opus, large WAV) or, for a WAV, loaded whole in RAM.
   Part of the stream library, so this code runs from RAM like the decoders
   (CMakeLists.txt). Until 1.1.10 these readers sat in main.c, in ROM: on
   the fx-CG50 the OS maps ROM page by page, and the read path, touched once
   per Opus packet, kept being mapped in again; each time the line stalled
   (1.1.10 gap meter: 926 of about 1160 gaps inside the packet read). */
typedef struct {
    const uint8_t *nor;          /* NOR base, uncached (0xa0000000) */
    const uint32_t *map;         /* NOR offset of each FLASHMAP_CHUNK of the file */
    uint32_t size;               /* file size */
    const uint8_t *samples;      /* WAV loaded in RAM: first sample byte; NULL: from NOR */
    uint32_t pcm_offset;         /* WAV from NOR: file offset of the sample data */
    unsigned bits;               /* WAV: 8 or 16 */
} nor_source_t;
/* mp3_source_read_t: copy n bytes at a file offset; -1 past the end. */
int nor_source_read(void *context,uint32_t offset,void *data,size_t n);
/* pcm_fetch_t: signed 16-bit WAV sample at an index (8-bit scaled by 256;
   16-bit read bytewise, the data chunk may be unaligned). */
int16_t nor_source_sample(void *context,uint32_t index);
#endif
