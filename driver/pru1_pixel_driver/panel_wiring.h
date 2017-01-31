#include <stdint.h>

#define N_BITS   4

#ifdef SMALL_P10

#define W_PANEL 32
#define H_PANEL 16
#define N_LINES  4
#define V_LAYOUT 1  // wire vertically first, then horizontally
#define B_LEN 8
#define MAX_PANELS 10

#define R1_VAL (1<<0)
#define G1_VAL (1<<1)
#define B1_VAL (1<<2)
#define R2_VAL (1<<3)
#define G2_VAL (1<<4)
#define B2_VAL (1<<5)

#else

#define W_PANEL 32
#define H_PANEL 32
#define N_LINES 8
#define H_LAYOUT 1  // horizontal first, then vertical
#define B_LEN 16
#define N_BITS 5
#define MAX_PANELS 10
#define FB_64 1

#define R1_VAL (1U<<2)
#define G1_VAL (1U<<1)
#define B1_VAL (1U<<0)
#define R2_VAL (1U<<5)
#define G2_VAL (1U<<4)
#define B2_VAL (1U<<3)

#endif