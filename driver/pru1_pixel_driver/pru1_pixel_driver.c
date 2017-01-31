/*
 * Copyright (C) 2015 Texas Instruments Incorporated - http://www.ti.com/
 *
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions
 * are met:
 *
 *	* Redistributions of source code must retain the above copyright
 *	  notice, this list of conditions and the following disclaimer.
 *
 *	* Redistributions in binary form must reproduce the above copyright
 *	  notice, this list of conditions and the following disclaimer in the
 *	  documentation and/or other materials provided with the
 *	  distribution.
 *
 *	* Neither the name of Texas Instruments Incorporated nor the names of
 *	  its contributors may be used to endorse or promote products derived
 *	  from this software without specific prior written permission.
 *
 * THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS
 * "AS IS" AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT
 * LIMITED TO, THE IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR
 * A PARTICULAR PURPOSE ARE DISCLAIMED. IN NO EVENT SHALL THE COPYRIGHT
 * OWNER OR CONTRIBUTORS BE LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL,
 * SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT
 * LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES; LOSS OF USE,
 * DATA, OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND ON ANY
 * THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT
 * (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE
 * OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.
 */

#include <stdint.h>

#include <pru_cfg.h>
#include <pru_intc.h>
#include <pru_iep.h>
#include "rsc_table_pru.h"

volatile register uint32_t __R30;
volatile register uint32_t __R31;

#define PRU0

#define PRU_IEP_EVT	7
#define HOST_INT1	0x80000000


#include "panel_wiring.h"

#define HUB75_B    8 /* "P8.27"  pru1: pr1_pru1_pru_r30_8,  B   */
#define HUB75_LAT 10 /* "P8.28"  pru1: pr1_pru1_pru_r30_10, LAT */
#define HUB75_C    9 /* "P8.29"  pru1: pr1_pru1_pru_r30_9,  C   */
#define HUB75_OE  11 /* "P8.30"  pru1: pr1_pru1_pru_r30_11, OE  */
#define HUB75_CLK  6 /* "P8.39"  pru1: pr1_pru1_pru_r30_6,  CLK */
#define HUB75_A    7 /* "P8.40"  pru1: pr1_pru1_pru_r30_7,  A   */
#define HUB75_G2   4 /* "P8.41"  pru1: pr1_pru1_pru_r31_4,  G2  */
#define HUB75_B2   5 /* "P8.42"  pru1: pr1_pru1_pru_r31_5,  B2  */
#define HUB75_B1   2 /* "P8.43"  pru1: pr1_pru1_pru_r30_2,  B1  */
#define HUB75_R2   3 /* "P8.44"  pru1: pr1_pru1_pru_r30_3,  R2  */
#define HUB75_R1   0 /* "P8.45"  pru1: pr1_pru1_pru_r30_0,  R1  */
#define HUB75_G1   1 /* "P8.46"  pru1: pr1_pru1_pru_r30_1,  G1  */

//#define SET_OE()  asm(" SET R31, R31, " # HUB75_OE)
//#define CLR_OE()  asm(" CLR R31, R31, " # HUB75_OE)
//#define SET_LAT() asm(" CLR R31, R31, " # HUB75_LAT)
//#define CLR_LAT() asm(" CLR R31, R31, " # HUB75_LAT)
//#define DO_SET(bit) __R30 |=  (1UL<<bit)
//#define DO_CLR(bit) __R30 &= ~(1UL<<bit)

#define STR(bit) #bit
#define DO_SET(bit) asm(" SET R30, R30, " STR( bit ))
#define DO_CLR(bit) asm(" CLR R30, R30, " STR( bit ))
//#define DO_TOG(bit) __R30 ^=  (1UL<<bit)


#define COLORMASK ~0x7FUL  // 6 colors + CLK

#define LSMASK ~((1UL << HUB75_A) | (1UL << HUB75_B) | (1UL << HUB75_C))

static const uint32_t line_setting[] = {
	      0        |       0        |       0        , 
	(1 << HUB75_A) |       0        |       0        , 
	      0        | (1 << HUB75_B) |       0        , 
	(1 << HUB75_A) | (1 << HUB75_B) |       0        , 
	      0        |       0        | (1 << HUB75_C) , 
	(1 << HUB75_A) |       0        | (1 << HUB75_C) , 
	      0        | (1 << HUB75_B) | (1 << HUB75_C) , 
	(1 << HUB75_A) | (1 << HUB75_B) | (1 << HUB75_C) 
};

static void set_line_output(uint8_t line)
{
	__R30 = ( __R30 & LSMASK ) | line_setting[line];	
}

static void config_ocp(){
	/* Clear SYSCFG[STANDBY_INIT] to enable OCP master port */
	CT_CFG.SYSCFG_bit.STANDBY_INIT = 0;
}

#define COLOR_MIN  100UL
#define BIT0_DELAY 1*COLOR_MIN
#define BIT1_DELAY 2*COLOR_MIN
#define BIT2_DELAY 4*COLOR_MIN
#define BIT3_DELAY 8*COLOR_MIN
#define BIT4_DELAY 16*COLOR_MIN
#define DIM_DELAY  1500UL
#define DIM_TIMER  5

void iep_timer_config(void)
{
	CT_IEP.TMR_GLB_CFG_bit.CNT_EN = 0;			/* Disable counter */
	CT_IEP.TMR_CNT = 0x0;						/* Reset Count register */
	CT_IEP.TMR_GLB_STS_bit.CNT_OVF = 0x1;		/* Clear overflow status register */
	CT_IEP.TMR_CMP0 = BIT0_DELAY;               /* Set compare0 value */
	CT_IEP.TMR_CMP1 = BIT1_DELAY;               /* Set compare1 value */
	CT_IEP.TMR_CMP2 = BIT2_DELAY;               /* Set compare2 value */
	CT_IEP.TMR_CMP3 = BIT3_DELAY;               /* Set compare3 value */
	CT_IEP.TMR_CMP4 = BIT4_DELAY;				/* Set compare4 value */
	CT_IEP.TMR_CMP5 = DIM_DELAY;				/* Set compare5 value */
	CT_IEP.TMR_CMP_STS_bit.CMP_HIT = 0xFF;		/* Clear compare status */
	CT_IEP.TMR_COMPEN_bit.COMPEN_CNT = 0x0;             /* Disable compensation */
	CT_IEP.TMR_CMP_CFG_bit.CMP0_RST_CNT_EN = 0x0;       /* Disable CMP0 and reset on event */
	CT_IEP.TMR_CMP_CFG_bit.CMP_EN = 0x0;        /* disable all compares */
	CT_IEP.TMR_GLB_CFG_bit.DEFAULT_INC = 0x1;	/* Configure incr value */

	CT_INTC.SECR0 = 0xFFFFFFFF;                 /* Clear the status of all interrupts */
	CT_INTC.SECR1 = 0xFFFFFFFF;
}

void iep_timer_start(unsigned int cmp)
{
	/* Enable Compare Value */
	CT_IEP.TMR_CMP_CFG_bit.CMP_EN = (1 << cmp);

	/* Enable Timer  */
	CT_IEP.TMR_GLB_CFG_bit.CNT_EN = 0x1;          /* Enable counter */
}

void iep_timer_wait(void)
{
#if 1
		/* Detect IEP Timer interrupt */
	do {
		while ((__R31 & HOST_INT1) == 0) {
		}
		/* Verify that the IEP is the source of the interrupt */
	} while (CT_INTC.HIPIR1 != PRU_IEP_EVT);
#else
	/* Poll until R31.31 is set */
	do {
		while ((__R31 & 0x80000000) == 0) {
		}
		/* Verify that the IEP is the source of the interrupt */
	} while ((CT_INTC.SECR0 & (1 << 7)) == 0);
#endif
	
	
	
	/* Disable IEP Timer */
	CT_IEP.TMR_GLB_CFG_bit.CNT_EN = 0x0;  /* Disable counter */
	CT_IEP.TMR_CNT = 0x0;                 /* Clear counter */

	/* Clear IEP Timer system event */
	//CT_IEP.TMR_CMP_STS = (1 << cmp);
	CT_IEP.TMR_CMP_STS = 0xFF;
	__delay_cycles(4);
	// CT_INTC.SECR0 = (1 << PRU_IEP_EVT);
	CT_INTC.SICR_bit.STS_CLR_IDX = PRU_IEP_EVT;
}

#if 0
/* this is not required / wanted when the resource table sets up the interrupt controller */

static void intc_config(void){

	/* Set interrupts 0 to 31 to active high */
	CT_INTC.SIPR0 = 0xFFFFFFFF;

	/* Clear mapping for sys evt 4 to 7 */
	CT_INTC.CMR1 = 0x00000000;

	/* Set event 7 to channel 0 */
	CT_INTC.CMR1_bit.CH_MAP_7 = 0x0;

	/* Map channel 0 to host 0 */
	CT_INTC.HMR0_bit.HINT_MAP_1 = 0x1;   /// so look for it on __R31[31]

	/* Enable system event (7th bit = sys interrupt 7) */
	CT_INTC.ESR0 = 0x80;

	/* Enable host interrupt 1 */
	CT_INTC.HIER = 0x2;

	/* Globally enable interrupts */
	CT_INTC.GER = 0x1;
}
#endif

#define nop() asm(" ")
#define nop2() asm(" MOV R14, R14")

#define BITCLOCK 25

static void shift_scanline(volatile uint8_t *scanline, uint16_t scanlen) {
	uint8_t color;
	uint16_t i;
	register uint32_t output_masked = __R30 & COLORMASK;
	// careful... this C code checked carefully for ASM tightness
	// 8 cycles would be 25 MHz... should be evenly spaced
	// but hard to tell with my equipment
#if BITCLOCK == 25
	for (i=0; i< scanlen; i++) {
		color = *scanline;    					// 3 cycles
		DO_CLR(HUB75_CLK);    					// 1 cycle
		__R30 = output_masked | color ; 		// 1 cycle
		nop();								    // 1 cycle nop
		scanline++;								// 1 cycle
		DO_SET(HUB75_CLK);   					// 1 cycle		
	}
	__delay_cycles(4);	
	__R30 = output_masked;
#elif BITCLOCK == 20 // 20 MHz
	for (i=0; i< scanlen; i++) {
		color = *scanline;    					// 3 cycles
		DO_CLR(HUB75_CLK);    					// 1 cycle
		nop();
		__R30 = output_masked | color ; 		// 1 cycle (clock also set to 0)
		nop();
		nop();								    // 1 cycle nop
		nop();
		scanline++;								// 1 cycle
		DO_SET(HUB75_CLK);   					// 1 cycle		
	}
	__delay_cycles(4);	
	__R30 = output_masked;
	
#else
	for (i=0; i< scanlen; i++) {
		color = *scanline;    					// 3 cycles
		nop2();
		DO_CLR(HUB75_CLK);    					// 1 cycle
		nop2();
		nop2();
		nop2();
//		nop2();
		nop2();
		__R30 = output_masked | color ; 		// 1 cycle
		nop2();
		nop2();
		nop2();
		nop2();
		scanline++;								// 1 cycle
		DO_SET(HUB75_CLK);   					// 1 cycle		
	}
	__delay_cycles(6);	
	__R30 = output_masked;
	
#endif

}

/* 
	what should be our delays? 
	clock is 200 MHz
	say we have a total screen refresh at 200 Hz.. 
	8 lines, 4 color bits per screen (15 color intervals 8,4,2,1)

	200 M / ( 200 * 8 * 15 ) .. ~ 8333 cycles per color interval

    does this make sense?  whats the fastest we can clock out a scanline?

	scanlen ~ 32 * n-panels ~ 320 max
    frequency is say 20 MHz..  that's 10 cycles per output
    or 3200 cycles per color interval.. so.. works!


*/




/* Shared memory setup */
//#pragma DATA_SECTION(shared, ".share_buff")
//volatile far struct shared_mem shared = { 0, 32 * 2 };

#pragma DATA_SECTION(buffer, ".share_buff")
volatile far uint8_t buffer[32*32*10];
uint16_t scanlen;

uint16_t load_test_pattern(volatile far uint8_t *shared);

void main_loop(void)
{
	uint8_t *scanline;
	uint8_t line, bit;
	
	// start the timer... since the loop expects one running
	iep_timer_start(3); 
	
	//shared.scanlen = 64;
	
	DO_CLR(HUB75_LAT);
	
	
	while(1) {
		// do all color bits for each line and then move on to next line
				for (line = 0; line < N_LINES; line++) {
					for (bit = 0; bit < N_BITS; bit++) {
#if 0				
				scanline = buffer + (line * N_BITS + bit) * scanlen ;
				shift_scanline( scanline, scanlen );
				iep_timer_wait();
				DO_SET(HUB75_OE);
				__delay_cycles(400);
				DO_SET(HUB75_LAT);
				__delay_cycles(400);
				set_line_output(line);
//				__delay_cycles(40);
				iep_timer_start(4);
				iep_timer_wait();
				iep_timer_start(bit);
				DO_CLR(HUB75_LAT);
				DO_CLR(HUB75_OE);
#else
				scanline = buffer + (line * N_BITS + bit) * scanlen ;
				iep_timer_wait();
				DO_SET(HUB75_OE);
				__delay_cycles(4);
				shift_scanline( scanline, scanlen );
				__delay_cycles(40);
				DO_SET(HUB75_LAT);
				__delay_cycles(80);
				DO_CLR(HUB75_LAT);
				// nop2();
				iep_timer_start(DIM_TIMER);
				set_line_output(line);
				iep_timer_wait();
				iep_timer_start(bit);
				DO_CLR(HUB75_OE);
#endif
			
			}
		}
	}
	
}

void main(void) {
	volatile uint8_t *scanline;
/*
	recommended clock frequency.. 25 MHz
	samples data on clock high
	moves data on latch high.. holds on latch low
		recommended latch pulse.. 200 ns
	enable output on LOW!
	
	
	The pixel driver does a continuous loop
	
	Start a new frame. (check if new data?)
		for each scanline
			for each bitfield
				* get scanline data for scanline & bitfiled
				* shift data for scanline
				* wait for end of previous end timer
				* turn off enable (set high!)
				* enable latch
				* set ABCD
				* start timer for bitfield
				* enable on (set low)
			signal EOL ?
		signal EOF ?
*/

    config_ocp();
	DO_SET(HUB75_OE);
	DO_SET(HUB75_LAT);
	
	scanlen = load_test_pattern(buffer);
	
    iep_timer_config();
    //intc_config();
	
	main_loop();

	// __halt();  // never gets here... 
}
