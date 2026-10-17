#include "tx-feed.h"
#include <gint/intc.h>
#include <gint/timer.h>
#include <gint/mpu/dma.h>
#include <gint/cpu.h>
#include <gint/clock.h>
#define REG16(n) (*(volatile uint16_t *)(0xa4410000u+(n)))
#define REG8(n) (*(volatile uint8_t *)(0xa4410000u+(n)))
#define SCR REG16(8)
#define TDR REG8(12)
#define FSR REG16(16)
#define FCR REG16(24)
#define FDR REG16(28)
#define TDR_PHYSICAL 0x0441000cu
/* DMARS2 selects the request sources of channels 4 (low byte) and 5 (high
   byte). The SH7305 is undocumented; the SH7724 map puts it at 0xfe009008
   and the SCIF transmit sources at 0x21/0x25/0x29/0x2d/0x31/0x35 (MID/RID).
   The open routine probes these and keeps the first one that moves data. */
#define DMARS2 (*(volatile uint16_t *)0xfe009008u)
#define DMA_CHANNEL 5
#define DEI5_EVENT 0xba0
#define CH (SH7305_DMA.DMA5)
#define DMA SH7305_DMA
static tx_feed_t *active;
/* IMASK=15 around the ring/DMA bookkeeping. Inline rather than gint's
   cpu_atomic_*, which live in ROM: the fx-CG50 maps ROM from NOR one page
   per TLB miss with interrupts blocked (CMakeLists.txt), and tx_feed_kick()
   runs once per frame. Not nested anywhere here. */
#if defined(__sh__)
static inline uint32_t irq_mask(void)
{
    uint32_t sr, masked;
    __asm__ volatile("stc sr,%0\n\tmov %0,%1\n\tor %2,%1\n\tldc %1,sr"
        :"=&r"(sr),"=&r"(masked):"r"(0xf0u):"memory");
    return sr;
}
static inline void irq_restore(uint32_t saved)
{
    uint32_t sr;
    __asm__ volatile("stc sr,%0\n\tand %1,%0\n\tor %2,%0\n\tldc %0,sr"
        :"=&r"(sr):"r"(~0xf0u),"r"(saved&0xf0u):"memory");
}
#else
static inline uint32_t irq_mask(void) { return 0; }    /* host syntax checks */
static inline void irq_restore(uint32_t saved) { (void)saved; }
#endif
static void dma_precompute(tx_feed_t *f);
static const unsigned dmars_candidates[]={0x21,0x29,0x25,0x2d,0x31,0x35};
static uint32_t physical(const void *p)
{
    uint32_t a=(uint32_t)(uintptr_t)p;
    return (a>=0x80000000u && a<0xc0000000u) ? a & 0x1fffffffu : a;
}
static void dma_disable(void)
{
    CH.CHCR.DE=0;
    CH.CHCR.TE=0;
    DMA.OR.AE=0; DMA.OR.NMIF=0;
}
static void dma_arm(const uint8_t *src, uint32_t n)
{
    CH.CHCR.DE=0;
    CH.SAR=physical(src);
    CH.DAR=TDR_PHYSICAL;
    CH.TCR=n;
    /* DM=00 fixed, SM=01 increment, RS=1000 DMARS, TB=0 cycle steal,
       TS=0000 byte, IE=1. */
    CH.CHCR.lword=0x00001804u;
    CH.CHCR.TE=0;
    DMA.OR.DME=1;
    CH.CHCR.DE=1;
}
/* Arm the next chunk from the ring. Caller holds interrupts off or is the
   ISR itself. */
static void dma_next(tx_feed_t *f)
{
    uint32_t r=f->ring.read, used=f->ring.write-r;
    /* Below a full chunk, keep going with what is there (down to ~1.1 ms:
       1 KiB at 921 600 B/s, 2 KiB with Pphi x2) rather than declaring an
       underrun at the first slow moment of the producer; 8 KiB chunks made
       that threshold 8.9 ms deep. */
    uint32_t n=used>=TX_FEED_CHUNK ? TX_FEED_CHUNK : (f->eof || used>=f->min_chunk ? used : 0);
    /* A chunk never crosses the end of the ring buffer (the DMA reads a
       flat address range); after a pause the read cursor may be unaligned. */
    uint32_t to_end=TX_RING_SIZE-(r&TX_RING_MASK);
    if(n>to_end) n=to_end;
    if(!n) {
        f->armed=0;
        if(f->eof) { f->complete=1; f->running=0; }
        else { f->fault=1; f->running=0; }
        return;
    }
    /* armed is published only once TCR holds n: tx_feed_free() reads
       armed and TCR separately, and armed = n with the old TCR (0 at a chunk
       end) counted a whole chunk as sent (0.9.6: ring.write - ring.read
       reached 147 456 > TX_RING_SIZE in 261015_11). While armed is 0 the
       producer only underestimates its space. */
    dma_arm(f->ring.bytes+(r&TX_RING_MASK),n);
    f->armed=n;
    dma_precompute(f);
}
/* Arm-first re-arm (0.9.4). At a chunk end the SCIF FIFO holds 8-16 bytes,
   4.3-8.7 us at Pphi x2, and the DEI5 entry plus bookkeeping plus re-arm
   took longer about half of the time: a dip of a few idle bytes at every
   other chunk boundary, 22-56 per second, the audible "clicks" of 0.9.2/3
   (261014_44..49, 96 kHz recordings). The chunk after the armed one is
   computed ahead of time, and the ISR's first action is to restart the
   channel; everything else follows. 261014_49: 48 -> 0.5 dips per second,
   -5.4 dB(A). */
static void dma_precompute(tx_feed_t *f)
{
    f->next_n=0;
    if(!f->armed) return;
    uint32_t r=f->ring.read+f->armed, used=f->ring.write-r;
    /* A partial chunk is fine: with the restart first, every boundary is
       gapless, so a shorter chunk only means an earlier (gapless) boundary.
       Requiring a whole chunk left MP3 (48 KiB bursts every 26 ms, nothing
       in between) on the slow path at every third boundary (261015_00).
       tx_feed_kick refreshes this after every write, so by the boundary
       the chunk is as long as the data allows. */
    if(used<f->min_chunk) return;                /* nothing worth arming yet */
    uint32_t n=used>TX_FEED_CHUNK ? TX_FEED_CHUNK : used;
    uint32_t to_end=TX_RING_SIZE-(r&TX_RING_MASK); if(n>to_end) n=to_end;
    f->next_src=physical(f->ring.bytes+(r&TX_RING_MASK));
    f->next_n=n;
}
static void dma_isr(void *context)
{
    tx_feed_t *f=context;
    if(f->next_n && f->running && !f->paused) {
        /* The register sequence of dma_arm minus DAR/DME: a whole-word
           CHCR write alone does not restart the channel on this chip. */
        CH.CHCR.DE=0;
        CH.SAR=f->next_src;
        CH.TCR=f->next_n;
        CH.CHCR.lword=0x00001804u;
        CH.CHCR.TE=0;
        CH.CHCR.DE=1;
        if(!CH.CHCR.TE && CH.CHCR.DE) {
            /* armed first, then read: in between the producer sees the new
               chunk as armed with TCR ~ next_n and the old read cursor, an
               underestimate; the other order over-counted by up to a chunk. */
            uint32_t done=f->armed;
            f->armed=f->next_n;
            f->ring.read+=done;
            f->ignore_gap=0;
            dma_precompute(f);
            return;
        }
        f->armfirst_failed++;                    /* did not take: full path below */
    }
    CH.CHCR.DE=0; CH.CHCR.TE=0;
    DMA.OR.AE=0; DMA.OR.NMIF=0;
    if(!f->running) return;
    uint32_t done=f->armed;
    f->armed=0;                      /* before read moves (see dma_next) */
    f->ring.read+=done;
    unsigned fill=(FDR>>8)&31u;
    if(!f->ignore_gap && !fill && (FSR&0x40)) f->gaps++;
    f->ignore_gap=0;
    if(f->paused) { f->armed=0; return; }
    dma_next(f);
}
#define PROBE_BLOCK 4096u
/* The 2026-10-14 scan of all 256 DMARS codes found exactly one that moves
   data to TDR: 0x21, and only while TE is set (with TE off the SCIF raises
   no request at all, which is what made the first probe, expecting a
   partial transfer, reject it). So the pacing test runs with TE on: a
   4 KiB block takes 4.4 ms on the line, an unpaced source would finish in
   microseconds. Paced means TCR is still decreasing between two samples. */
static int dma_probe(tx_feed_t *f)
{
    uint8_t *block=f->ring.bytes;        /* ring is empty at open */
    for(unsigned i=0;i<PROBE_BLOCK;i++) block[i]=0;   /* line low */
    for(uintptr_t a=(uintptr_t)block & ~(uintptr_t)31;a<(uintptr_t)block+PROBE_BLOCK;a+=32)
        __asm__ volatile("ocbwb @%0" :: "r"(a) : "memory");
    DMA.OR.DME=1;
    for(unsigned i=0;i<sizeof dmars_candidates/sizeof *dmars_candidates;i++) {
        SCR=0; FCR=6; FCR=0; FSR=FSR & (uint16_t)~0x60;
        SCR=0xa0; /* TE and TIE: the transmit-FIFO request is live. */
        DMARS2=(uint16_t)((DMARS2 & 0x00ffu) | (dmars_candidates[i]<<8));
        dma_arm(block,PROBE_BLOCK);
        for(volatile unsigned spin=0;spin<5000;spin++) {}
        uint32_t t1=CH.TCR;
        for(volatile unsigned spin=0;spin<5000;spin++) {}
        uint32_t t2=CH.TCR;
        /* Let it finish either way, then flush the FIFO. A paced source
           needs 4.4 ms for the block; count how long it takes. */
        unsigned spins=0;
        while(spins<2000000 && CH.TCR) spins++;
        int paced=(t1<PROBE_BLOCK && t2<t1 && t2>0) || (t2>0 && spins>2000);
        dma_disable();
        for(unsigned spin=0;spin<200000 && !(FSR&0x40);spin++) {}
        SCR=0; FCR=6; FCR=0; FSR=FSR & (uint16_t)~0x60;
        if(paced) { f->dmars=dmars_candidates[i]; SCR=0xa0; return 0; }
    }
    SCR=0x20;
    return -1;
}
/* Timer fallback: the 0.5.1 feeder. */
static int timer_feed(void *context)
{
    tx_feed_t *f=context;
    uint32_t r=f->ring.read, w=f->ring.write;
    atomic_signal_fence(memory_order_acquire);
    unsigned fill=(FDR>>8)&31u;
    if(fill>16) { f->fault=3; f->running=0; return TIMER_STOP; }
    if(r==w) {
        if(f->eof) { f->complete=1; f->running=0; return TIMER_STOP; }
        f->fault=1; f->running=0; return TIMER_STOP;
    }
    if(!f->ignore_gap && !fill && (FSR&0x40) && r) f->gaps++;
    f->ignore_gap=0;
    unsigned n=16-fill;
    if(n>w-r) n=w-r;
    for(unsigned i=0;i<n;i++) TDR=f->ring.bytes[(r+i)&TX_RING_MASK];
    atomic_signal_fence(memory_order_release);
    f->ring.read=r+n;
    if(n) f->stalled=0;
    else if(++f->stalled>=f->stall_limit) { f->fault=2; f->running=0; return TIMER_STOP; }
    return TIMER_CONTINUE;
}
int tx_feed_open(tx_feed_t *f)
{
    f->backend=0; f->timer=-1; f->dmars=0;
    /* Byte rate from the live peripheral clock: Pphi/4 bits per second,
       8 bits per byte. The thresholds below were tuned in bytes at
       921 600 B/s; keep them the same in time. */
    uint32_t pphi=(uint32_t)clock_freq()->Pphi_f;
    f->byte_rate=pphi ? pphi/32u : 921600u;
    f->min_chunk=f->byte_rate/900u;               /* 1024 / 2048 bytes, ~1.1 ms */
    f->timer_us=16u*1000000u/(2u*f->byte_rate);   /* 8 us / 4 us */
    if(f->timer_us<2u) f->timer_us=2u;
    f->stall_limit=1000000u/f->timer_us;          /* 1 s */
    tx_feed_reset(f);
    active=f;
    f->os_dmars2=DMARS2;
    if(dma_probe(f)==0) {
        intc_handler_function(DEI5_EVENT,GINT_CALL(dma_isr,(void *)f));
        /* The re-arm must run before anything else; the old level comes
           back at close. */
        f->os_dei5_priority=intc_priority(INTC_DMA_DEI5,15);
        f->backend=TX_FEED_DMA;
        return f->backend;
    }
    DMARS2=f->os_dmars2;
    f->timer=timer_configure(TIMER_TMU,f->timer_us,GINT_CALL(timer_feed,(void *)f));
    if(f->timer<0) return -1;
    f->backend=TX_FEED_TIMER;
    return f->backend;
}
void tx_feed_reset(tx_feed_t *f)
{
    if(f->backend==TX_FEED_DMA) dma_disable();
    f->ring.read=f->ring.write=0;
    f->next_n=0;
    f->running=f->complete=f->eof=f->paused=0;
    f->fault=0; f->gaps=0; f->armed=0; f->stalled=0; f->ignore_gap=1; f->missed_irq=0;
}
void tx_feed_start(tx_feed_t *f)
{
    if(f->running || f->fault) return;
    f->running=1; f->paused=0; f->ignore_gap=1;
    if(f->backend==TX_FEED_DMA) {
        uint32_t irq=irq_mask();
        dma_next(f);
        irq_restore(irq);
    } else {
        timer_feed(f);
        if(f->running) timer_start(f->timer);
    }
}
void tx_feed_kick(tx_feed_t *f)
{
    if(f->backend!=TX_FEED_DMA || !f->running || f->paused) return;
    /* The ISR arms chunks itself; only an idle channel at end of stream
       (partial tail) needs a push from the producer. */
    uint32_t irq=irq_mask();
    if(f->armed && f->next_n<TX_FEED_CHUNK) dma_precompute(f);   /* grow the next chunk with new data */
    if(!f->armed && f->running) dma_next(f);
    /* Safety net: if the chunk finished (TE set) and no DEI5 interrupt
       consumed it, the event code is wrong for this chip; re-arm here. */
    else if(f->armed && CH.CHCR.TE) { f->missed_irq++; dma_isr(f); }
    irq_restore(irq);
}
void tx_feed_pause(tx_feed_t *f)
{
    if(!f->running) return;
    f->paused=1;
    if(f->backend==TX_FEED_DMA) {
        /* Let the armed chunk finish; the ISR then parks the channel. */
        uint32_t irq=irq_mask();
        if(f->armed) {
            CH.CHCR.DE=0;
            /* Bytes not yet moved stay in the ring: TCR counts what is left. */
            uint32_t left=CH.TCR, done=f->armed>left ? f->armed-left : 0;
            f->armed=0;
            f->ring.read+=done;
            CH.CHCR.TE=0;
        }
        irq_restore(irq);
    } else timer_pause(f->timer);
}
void tx_feed_resume(tx_feed_t *f)
{
    if(!f->running || !f->paused) return;
    f->paused=0; f->ignore_gap=1;
    if(f->backend==TX_FEED_DMA) {
        uint32_t irq=irq_mask();
        dma_next(f);
        irq_restore(irq);
    } else timer_start(f->timer);
}
void tx_feed_close(tx_feed_t *f)
{
    if(f->backend==TX_FEED_DMA) {
        dma_disable(); SCR=0x20;
        intc_priority(INTC_DMA_DEI5,f->os_dei5_priority);
        DMARS2=f->os_dmars2;
    }
    else if(f->timer>=0) { timer_stop(f->timer); f->timer=-1; }
    f->backend=0;
    f->running=0;
    if(active==f) active=NULL;
}
uint32_t tx_feed_consumed(const tx_feed_t *f)
{
    return f->ring.read;
}
uint32_t tx_feed_free(const tx_feed_t *f)
{
    uint32_t r=f->ring.read;
    if(f->backend==TX_FEED_DMA) {
        for(;;) {
            uint32_t armed=f->armed, left=CH.TCR, r2=f->ring.read;
            if(r2!=r) { r=r2; continue; }       /* a chunk completed meanwhile */
            if(armed && left<=armed) r+=armed-left;
            break;
        }
    }
    return TX_RING_SIZE-(f->ring.write-r);
}
