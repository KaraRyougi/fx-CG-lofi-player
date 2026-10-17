#pragma once
#include <stdint.h>
typedef volatile struct { union { uint32_t lword; } FRQCR; union { uint32_t lword; } FSICLKCR; uint32_t pad[8];
  union { uint32_t lword; } FLLFRQ; union { uint32_t lword; } PLLCR; union { uint32_t lword; } SSCGCR; } sh7305_cpg_t;
extern sh7305_cpg_t SH7305_CPG;
