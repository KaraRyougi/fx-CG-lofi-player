#ifndef LOFI100_TX_RING_H
#define LOFI100_TX_RING_H
#include <stdint.h>
#include <string.h>
#include <stdatomic.h>
#include "sdm.h"
/* 128 KiB = four 32 KiB DMA chunks (0.9.4). With two, the chunk after the
   armed one was often still being written when the boundary came, and the
   arm-first restart fell back to the slow path (0.2-0.8 dips per second
   left in 261014_49). The producer now stays at least one chunk ahead.
   71 ms of output at Pphi x2. */
#define TX_RING_SIZE 131072u
#define TX_RING_MASK (TX_RING_SIZE-1u)
/* One foreground producer, one timer ISR consumer. SH aligned 32-bit loads
   and stores are atomic; publication fences keep the payload before cursor. */
typedef struct {
    uint8_t bytes[TX_RING_SIZE] __attribute__((aligned(32)));
    volatile uint32_t read, write;
} tx_ring_t;
static inline uint32_t tx_ring_used(const tx_ring_t *q)
{
    return q->write-q->read;
}
static inline int tx_ring_write(tx_ring_t *q, const uint8_t *data, uint32_t n)
{
    uint32_t w=q->write;
    if(n>TX_RING_SIZE || w-q->read>TX_RING_SIZE-n) return -1;
    uint32_t first=TX_RING_SIZE-(w&TX_RING_MASK);
    if(first>n) first=n;
    memcpy(q->bytes+(w&TX_RING_MASK),data,first);
    memcpy(q->bytes,data+first,n-first);
#if defined(__sh__)
    /* A DMA consumer reads physical memory: write the lines back. */
    for(uint32_t i=0;i<n;i+=32) {
        uintptr_t a=(uintptr_t)(q->bytes+((w+i)&TX_RING_MASK)) & ~(uintptr_t)31;
        __asm__ volatile("ocbwb @%0" :: "r"(a) : "memory");
    }
    __asm__ volatile("ocbwb @%0" :: "r"((uintptr_t)(q->bytes+((w+n-1)&TX_RING_MASK)) & ~(uintptr_t)31) : "memory");
#endif
    atomic_signal_fence(memory_order_release);
    q->write=w+n;
    return 0;
}
/* One frame (sdm.h frame code) generated straight into the ring (1.1.1),
   without a space check: the caller has made sure (tx_feed_free) that the
   DMA has already moved past the bytes being overwritten. Every write is a
   whole frame, so the write position is a multiple of 64 and the frame is
   two whole 32-byte cache lines. On the SH each line starts with movca.l,
   which allocates the line without reading it from SDRAM (an ordinary
   store first fetched the 32 bytes about to be overwritten), and ends with
   ocbp: written back for the DMA, which reads memory, and dropped from the
   operand cache, which the CPU never reads it through again (ocbwb left it
   there, displacing the decoder's lines). An interrupt between the two
   that evicts the half-written line only writes garbage to bytes the DMA
   will not read before they are complete and published; the remaining
   stores then fetch the line as usual. */
typedef uint32_t tx_word_t __attribute__((may_alias));
static inline void tx_ring_put_frame(tx_ring_t *q, unsigned frame)
{
    uint32_t w=q->write;
    tx_word_t *p=(tx_word_t *)(void *)(q->bytes+(w&TX_RING_MASK));
    for(unsigned line=0;line<2;line++,p+=8) {
        unsigned s,e; sdm_line_run(frame,line,&s,&e);
        uint32_t v[8]; sdm_line_words(s,e,v);
#if defined(__sh__)
        __asm__ volatile("movca.l %0,@%1" :: "z"(v[0]), "r"(p) : "memory");  /* source: R0 only */
#else
        p[0]=v[0];
#endif
        for(unsigned k=1;k<8;k++) p[k]=v[k];
#if defined(__sh__)
        __asm__ volatile("ocbp @%0" :: "r"(p) : "memory");
#endif
    }
    atomic_signal_fence(memory_order_release);
    q->write=w+SDM_FRAME_BYTES;
}
static inline int tx_ring_frame(tx_ring_t *q,const uint8_t data[32])
{ return tx_ring_write(q,data,32); }
#endif
