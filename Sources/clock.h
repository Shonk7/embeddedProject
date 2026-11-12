#ifndef CLOCK_H
#define CLOCK_H

// clock.h: the clock speeds the whole firmware is built around. Anything that
// turns CPU cycles into time (delays, rate measurement, the ISR budget) uses
// these numbers, so they must match what SystemInit.c really sets up.

// Crystal X501. The Board Guide says 12 MHz, but the schematic's part number says
// 16 MHz. SystemInit.c is written for 12 MHz only (x 6 in the PLL = 72 MHz)
#define HSE_HZ		12000000U
#define HSI_HZ		8000000U	// internal clock the chip boots on

// Speeds SystemInit() sets up, and what each one runs
#define SYSCLK_HZ	72000000U	// CPU, AHB, FSMC
#define PCLK2_HZ	72000000U	// APB2: GPIO, AFIO, EXTI, ADC1, SPI1
#define PCLK1_HZ	36000000U	// APB1: DAC (TIM2-7 run at 2 x PCLK1 = 72 MHz)
#define ADCCLK_HZ	12000000U	// PCLK2 / 6, limit is 14 MHz

#endif
