#ifndef CLOCK_H
#define CLOCK_H

// Crystal X501. Board Guide Ch 2 and the schematic symbol say 12 MHz, but the
// schematic part number reads "TSX-3225 16.0000MF...". 12 and 16 are both
// handled in SystemInit.c - the Stage 1 test tells you which one is fitted.
#define HSE_HZ		12000000U
#define HSI_HZ		8000000U

// Clock tree that SystemInit() sets up
#define SYSCLK_HZ	72000000U	// CPU, AHB, FSMC
#define PCLK2_HZ	72000000U	// APB2: GPIO, AFIO, EXTI, ADC1, SPI1
#define PCLK1_HZ	36000000U	// APB1: DAC (TIM2-7 run at 2 x PCLK1 = 72 MHz)
#define ADCCLK_HZ	12000000U	// PCLK2 / 6, limit is 14 MHz

#endif
