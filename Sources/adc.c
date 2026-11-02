#include "adc.h"

// The ADC needs 1 us after power-up (tSTAB). NOPs take at least a cycle each,
// so this is over 2 us at 72 MHz, which also covers the 2 ADC clocks CAL needs
#define POWER_UP_SPINS	150U

void adc_init(void) {
	uint32_t i;

	RCC->APB2ENR |= RCC_APB2ENR_IOPAEN | RCC_APB2ENR_ADC1EN;

	// PA3 analog input (CNF = 00, MODE = 00) disconnects the digital input stage
	GPIOA->CRL &= ~(GPIO_CRL_CNF3_Msk | GPIO_CRL_MODE3_Msk);

	// One channel, no scan, no interrupts, single conversion, right aligned.
	// Software start still goes through the trigger select, as EXTSEL = 111
	ADC1->CR1 = 0U;
	ADC1->CR2 = ADC_CR2_EXTTRIG | (7U << ADC_CR2_EXTSEL_Pos);

	ADC1->SMPR2 = (ADC1->SMPR2 & ~ADC_SMPR2_SMP3_Msk) | (ADC_SAMPLE_TIME << ADC_SMPR2_SMP3_Pos);

	// A sequence of one conversion (L = 0): channel 3
	ADC1->SQR1 = 0U;
	ADC1->SQR3 = ADC_AUDIO_CHANNEL << ADC_SQR3_SQ1_Pos;

	// The first write to ADON only wakes the ADC up
	ADC1->CR2 |= ADC_CR2_ADON;
	for (i = 0U; i < POWER_UP_SPINS; i++) {
		__NOP();
	}

	// Self calibration. RM0008 11.4 recommends this after every power-up
	ADC1->CR2 |= ADC_CR2_RSTCAL;
	while (ADC1->CR2 & ADC_CR2_RSTCAL) {
	}
	ADC1->CR2 |= ADC_CR2_CAL;
	while (ADC1->CR2 & ADC_CR2_CAL) {
	}
}
