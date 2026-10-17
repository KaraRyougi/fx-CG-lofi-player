#ifndef LOFI100_TX_FEED_H
#define LOFI100_TX_FEED_H
#include "tx-ring.h"
#include <stdint.h>
/* Serial FIFO feeder for gint-world playback. The producer writes modulated
   bytes into feed->ring; the backend moves them into the SCIF transmit FIFO.
   Preferred backend: DMAC channel 5 triggered by the SCIF transmit request
   (DMARS), re-armed chunk by chunk from the DEI5 interrupt. Fallback: an
   8 us TMU interrupt (4 us with Pphi x2) copying bytes by hand (the 0.5.1
   method). */
#define TX_FEED_TIMER 1
#define TX_FEED_DMA 2
#define TX_FEED_CHUNK 32768u     /* DMA chunk (35.6 ms at 921 600 B/s, 17.8 ms
                                    with Pphi x2); divides TX_RING_SIZE (4x). With
                                    tx_feed_free() the producer no longer waits
                                    for chunk ends, so a chunk can be large:
                                    the re-arm ISR runs at 28 / 56 Hz. */
typedef struct {
    tx_ring_t ring;
    volatile int running, complete, eof, paused;
    volatile int fault;          /* 1 underrun, 2 stalled, 3 FIFO overflow */
    volatile uint32_t gaps;      /* FIFO found empty while running */
    volatile uint32_t armed;     /* bytes in flight on the DMA channel */
    volatile int ignore_gap;
    volatile uint32_t missed_irq;  /* DMA chunks completed without a DEI5 interrupt */
    uint32_t stalled;
    int backend, timer;
    unsigned dmars;
    /* Derived from clock_freq()->Pphi_f at open: the SCIF moves Pphi/32
       bytes per second (bit clock Pphi/4, CKS=0, BRR=0): 921 600 B/s on
       the normal clock, 1 843 200 with Pphi x2. */
    uint32_t byte_rate;
    uint32_t next_src, next_n;   /* chunk after the armed one, restarted first in the ISR */
    volatile uint32_t armfirst_failed;  /* restarts that did not take (diagnostic) */
    uint32_t min_chunk;          /* smallest partial DMA chunk (~1.1 ms) */
    uint32_t timer_us;           /* TMU fallback period: half the FIFO drain time */
    uint32_t stall_limit;        /* timer ticks without progress before fault 2 (1 s) */
    /* OS state taken over at open and given back at close: the DMARS2
       request-source byte and the DEI5 interrupt priority. The OS may use
       the channel itself, and the add-in is relaunched into the same OS. */
    uint16_t os_dmars2;
    int os_dei5_priority;
} tx_feed_t;
/* SCIF must already be in clock-synchronous mode with TE set. Returns the
   backend chosen, or -1 if neither a DMA request nor a TMU is available. */
int tx_feed_open(tx_feed_t *f);
void tx_feed_reset(tx_feed_t *f);      /* ring and flags, between tracks/seeks */
void tx_feed_start(tx_feed_t *f);      /* begin consuming what is buffered */
void tx_feed_kick(tx_feed_t *f);       /* producer wrote data or set eof */
void tx_feed_pause(tx_feed_t *f);
void tx_feed_resume(tx_feed_t *f);
void tx_feed_close(tx_feed_t *f);      /* stops the backend; FIFO may still drain */
uint32_t tx_feed_consumed(const tx_feed_t *f);  /* bytes handed to the FIFO */
/* Free ring space counted from what the DMA has actually moved so far
   (read + armed - TCR), not from the last completed chunk. Producing into
   this space frame by frame keeps the CPU's rhythm at the 28.8 kHz frame
   rate instead of the chunk rate (0.8.0: +-112.5 Hz sidebands at -45 dB
   around a 1 kHz tone from 8 KiB bursts, 261014_24). */
uint32_t tx_feed_free(const tx_feed_t *f);
#endif
