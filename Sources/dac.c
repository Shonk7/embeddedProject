#include "dac.h"

// Sets up DAC channel 1 on PA4
void dac_init(void) {
	RCC->APB1ENR |= RCC_APB1ENR_DACEN;
	RCC->APB2ENR |= RCC_APB2ENR_IOPAEN;

	// Make PA4 analog (CNF = 00, MODE = 00). It's an output, but the DAC needs the
	// pin's digital side disconnected
	GPIOA->CRL &= ~(GPIO_CRL_CNF4_Msk | GPIO_CRL_MODE4_Msk);

	// Start at 1.65 V, the resting level for audio
	dac_write(DAC_MID_CODE);

	// Turn on channel 1. Leaving BOFF1 and TEN1 at 0 keeps the output buffer on
	// and makes each write update the pin straight away
	DAC->CR = DAC_CR_EN1;
}
