/* Big-endian SH4 check of the frame generator (src/sdm.h) and the
   movca.l/ocbp ring writer (src/tx-ring.h), run in the fxSDK image's GDB
   simulator by tests/test-sh-frames.sh: every frame code through
   sdm_frame_write and tx_ring_put_frame across the end of the ring,
   against a bitwise reference. The host tests
   run little-endian only; the SH stores the words swapped. The simulator
   has no cache model: movca.l is a store and ocbp does nothing there, so
   this checks the data, not the line allocation. Exit status 0 = pass,
   1+n / 129+n = frame code n (mod 128 / 64) wrong in the buffer / ring,
   250 = not big-endian, 251 = wrong count. -DNEG breaks one expected
   frame (the script's negative control: must fail). */
#include "tx-ring.h"
void *memcpy(void *d,const void *s,size_t n){ unsigned char *a=d; const unsigned char *b=s; while(n--) *a++=*b++; return d; }
void *memset(void *d,int c,size_t n){ unsigned char *a=d; while(n--) *a++=(unsigned char)c; return d; }
int memcmp(const void *x,const void *y,size_t n){ const unsigned char *a=x,*b=y; for(;n;n--,a++,b++) if(*a!=*b) return *a-*b; return 0; }
static tx_ring_t ring;
__attribute__((used)) static int run(void)
{
    if(*(const uint8_t *)&(uint32_t){1}) return 250;               /* must be big-endian */
    ring.read=ring.write=TX_RING_SIZE-64*100;                        /* wraps the ring end */
    unsigned codes=0;
    for(int sh=0;sh<2;sh++) for(unsigned n=0;n<=SDM_FRAME_BITS;n++) {
        if(sh) break;
        unsigned frame=n;
        uint8_t want[64], got[64] __attribute__((aligned(4)));
        /* reference, bit by bit: bit i is bit i%8 of byte i/8 */
        unsigned a=sh ? n/2 : n, b=sh ? n-n/2 : 0;
        unsigned s0=sh ? (256-a)/2 : (512-n)/2, s1=256+(256-b)/2;
        for(unsigned i=0;i<64;i++) want[i]=0;
        for(unsigned i=0;i<512;i++) if((i>=s0 && i<s0+a) || (sh && i>=s1 && i<s1+b)) want[i/8]|=(uint8_t)(1u<<(i%8));
#ifdef NEG
        if(n==300) want[37]^=0x10;
#endif
        sdm_frame_write(got,frame);
        if(memcmp(got,want,64)) return 1+(int)(n&127);
            uint32_t w=ring.write;
            memset(ring.bytes+(w&TX_RING_MASK),0x5a,64);
            tx_ring_put_frame(&ring,frame);
            if(ring.write!=w+64 || memcmp(ring.bytes+(w&TX_RING_MASK),want,64)) return 129+(int)(n&63);
            ring.read=ring.write;
        codes++;
    }
    return codes==(unsigned)(513*(1
        )) ? 0 : 251;
}
void __attribute__((noreturn)) exit_sim(int code)
{
    register int r4 __asm__("r4")=1, r5 __asm__("r5")=code;          /* SYS_exit */
    __asm__ volatile("trapa #34" :: "r"(r4), "r"(r5));
    for(;;) {}
}
__attribute__((used)) static uint32_t stack[4096] __attribute__((aligned(8)));
__asm__(".section .text.start,\"ax\"\n.global _start\n.align 2\n_start:\n"
        "\tmov.l 1f,r15\n\tmov.l 2f,r0\n\tjsr @r0\n\tnop\n\tmov r0,r4\n\tmov.l 3f,r1\n\tjmp @r1\n\tnop\n"
        "\t.align 2\n1:\t.long _stack+16384\n2:\t.long _run\n3:\t.long _exit_sim\n\t.text\n");
