#ifndef PRU_DEFS_H
#define PRU_DEFS_H

volatile register unsigned int __R31;
volatile register unsigned int __R30;
 
#define SIGNAL_EVENT(x) \
	do { \
		__R31 = (1 << 5) | ((x) - 16); \
	} while(0)


#ifndef PRU_CLK
/* default PRU clock (200MHz) */
#define PRU_CLK	200000000
#endif

/* NOTE: Do no use it for larger than 5 secs */
#define PRU_200MHz_sec(x)	((u32)(((x) * 200000000)))
#define PRU_200MHz_ms(x)	((u32)(((x) * 200000)))
#define PRU_200MHz_ms_err(x)	0
#define PRU_200MHz_us(x)	((u32)(((x) * 200)))
#define PRU_200MHz_us_err(x)	0
#define PRU_200MHz_ns(x)	((u32)(((x) * 2) / 10))
#define PRU_200MHz_ns_err(x)	((u32)(((x) * 2) % 10))

#if PRU_CLK != 200000000 
/* NOTE: Do no use it for larger than 5 secs */
#define PRU_sec(x)	((u32)(((u64)(x) * PRU_CLK)))
#define PRU_ms(x)	((u32)(((u64)(x) * PRU_CLK) / 1000))
#define PRU_ms_err(x)	((u32)(((u64)(x) * PRU_CLK) % 1000))
#define PRU_us(x)	((u32)(((u64)(x) * PRU_CLK) / 1000000))
#define PRU_us_err(x)	((u32)(((u64)(x) * PRU_CLK) % 1000000))
#define PRU_ns(x)	((u32)(((u64)(x) * PRU_CLK) / 1000000000))
#define PRU_ns_err(x)	((u32)(((u64)(x) * PRU_CLK) % 1000000000))
#else
/* NOTE: Do no use it for larger than 5 secs */
#define PRU_sec(x)	PRU_200MHz_sec(x)
#define PRU_ms(x)	PRU_200MHz_ms(x)
#define PRU_ms_err(x)	PRU_200MHz_ms_err(x)
#define PRU_us(x)	PRU_200MHz_us(x)
#define PRU_us_err(x)	PRU_200MHz_us_err(x)
#define PRU_ns(x)	PRU_200MHz_ns(x)
#define PRU_ns_err(x)	PRU_200MHz_ns_err(x)
#endif

#define DPRAM_SHARED	0x00010000

/* event definitions */
#define SYSEV_ARM_TO_PRU0	21
#define SYSEV_ARM_TO_PRU1	22
#define SYSEV_PRU0_TO_ARM	19
#define SYSEV_PRU0_TO_PRU1	17
#define SYSEV_PRU1_TO_ARM	20
#define SYSEV_PRU1_TO_PRU0	18

/* for communication with the host we have another set of events */
#define SYSEV_VR_ARM_TO_PRU0	24
#define SYSEV_VR_PRU0_TO_ARM	25
#define SYSEV_VR_ARM_TO_PRU1	26
#define SYSEV_VR_PRU1_TO_ARM	27

#define pru0_signal() (__R31 & (1U << 30))
#define pru1_signal() (__R31 & (1U << 31))

#ifdef PRU0
#define pru_signal()	pru0_signal()
#define SYSEV_OTHER_PRU_TO_THIS_PRU	SYSEV_PRU1_TO_PRU0
#define SYSEV_ARM_TO_THIS_PRU		SYSEV_ARM_TO_PRU0
#define SYSEV_THIS_PRU_TO_OTHER_PRU	SYSEV_PRU0_TO_PRU1
#define SYSEV_THIS_PRU_TO_ARM		SYSEV_PRU0_TO_ARM
#define SYSEV_VR_ARM_TO_THIS_PRU	SYSEV_VR_ARM_TO_PRU0
#define SYSEV_VR_THIS_PRU_TO_ARM	SYSEV_VR_PRU0_TO_ARM
#endif

#ifdef PRU1
#define pru_signal()	pru1_signal()
#define SYSEV_OTHER_PRU_TO_THIS_PRU	SYSEV_PRU0_TO_PRU1
#define SYSEV_ARM_TO_THIS_PRU		SYSEV_ARM_TO_PRU1
#define SYSEV_THIS_PRU_TO_OTHER_PRU	SYSEV_PRU1_TO_PRU0
#define SYSEV_THIS_PRU_TO_ARM		SYSEV_PRU1_TO_ARM
#define SYSEV_VR_ARM_TO_THIS_PRU	SYSEV_VR_ARM_TO_PRU1
#define SYSEV_VR_THIS_PRU_TO_ARM	SYSEV_VR_PRU1_TO_ARM
#endif

/* all events < 32 */
#define SYSEV_THIS_PRU_INCOMING_MASK	\
	(BIT(SYSEV_ARM_TO_THIS_PRU) | \
	 BIT(SYSEV_OTHER_PRU_TO_THIS_PRU) | \
	 BIT(SYSEV_VR_ARM_TO_THIS_PRU))

#define DELAY_CYCLES(x) \
	do { \
		unsigned int t = (x) >> 1; \
		do { \
			__asm(" "); \
		} while (--t); \
	} while(0)

#ifndef BIT
#define BIT(x) (1U << (x))
#endif

#endif
