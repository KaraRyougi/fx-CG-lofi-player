#pragma once
#include <stdint.h>
typedef struct { uint32_t SAR, DAR, TCR; union { uint32_t lword; struct { unsigned :1, LCKN:1, :2, RPT:3, DA:1, DO:1, :1, TS_32:2, HE:1, HIE:1, AM:1, AL:1, DM:2, SM:2, RS:4, DL:1, DS:1, TB:1, TS_10:2, IE:1, TE:1, DE:1; }; } CHCR; } sh7305_dma_channel_t;
typedef struct { sh7305_dma_channel_t DMA0,DMA1,DMA2,DMA3; union { uint16_t word; struct { unsigned CMS:4,:2,PR:2,:5,AE:1,NMIF:1,DME:1; }; } OR; uint8_t pad[14]; sh7305_dma_channel_t DMA4,DMA5; } sh7305_dma_t;
extern volatile sh7305_dma_t SH7305_DMA;
