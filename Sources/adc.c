// adc.c: one-time setup of ADC1 for the audio input. The per-sample calls
// (start, check, read) are inline in adc.h so the ISR can use them cheaply.

#include "adc.h"

// Delay after power-up. The ADC needs 1 us, and 150 NOPs is at least 2 us at 72 MHz
#define POWER_UP_SPINS	150U

// Sets up ADC1 to read PA3 one conversion at a time, started by software
void adc_init(void) {
	uint32_t i;

	// Clocks for port A (the pin) and ADC1, or their registers ignore our writes
	RCC->APB2ENR |= RCC_APB2ENR_IOPAEN | RCC_APB2ENR_ADC1EN;

	// Make PA3 analog (CNF = 00, MODE = 00), which disconnects its digital input
	GPIOA->CRL &= ~(GPIO_CRL_CNF3_Msk | GPIO_CRL_MODE3_Msk);

	// One channel, no interrupts, one conversion per start, right aligned.
	// EXTTRIG with EXTSEL = 111 is how software starts a conversion
	ADC1->CR1 = 0U;
	ADC1->CR2 = ADC_CR2_EXTTRIG | (7U << ADC_CR2_EXTSEL_Pos);

	// Set the sample time for channel 3 only
	ADC1->SMPR2 = (ADC1->SMPR2 & ~ADC_SMPR2_SMP3_Msk) | (ADC_SAMPLE_TIME << ADC_SMPR2_SMP3_Pos);

	// The sequence is one conversion long (L = 0), and it reads channel 3
	ADC1->SQR1 = 0U;
	ADC1->SQR3 = ADC_AUDIO_CHANNEL << ADC_SQR3_SQ1_Pos;

	// Power up the ADC and give it time to wake
	ADC1->CR2 |= ADC_CR2_ADON;
	for (i = 0U; i < POWER_UP_SPINS; i++) {
		__NOP();
	}

	// Run the ADC's self calibration, recommended after every power-up
	ADC1->CR2 |= ADC_CR2_RSTCAL;
	while (ADC1->CR2 & ADC_CR2_RSTCAL) {
	}
	ADC1->CR2 |= ADC_CR2_CAL;
	while (ADC1->CR2 & ADC_CR2_CAL) {
	}
}
