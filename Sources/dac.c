#include "dac.h"

void dac_init(void) {
	RCC->APB1ENR |= RCC_APB1ENR_DACEN;
	RCC->APB2ENR |= RCC_APB2ENR_IOPAEN;

	// PA4 analog (CNF = 00, MODE = 00). The DAC drives the pin through its analog
	// path, so the digital stage has to be disconnected even though this is an output
	GPIOA->CRL &= ~(GPIO_CRL_CNF4_Msk | GPIO_CRL_MODE4_Msk);

	// Load the middle of the range first, so the output wakes up at audio silence
	dac_write(DAC_MID_CODE);

	// Channel 1 on. BOFF1 = 0 keeps the output buffer on, and TEN1 = 0 means
	// writes update the pin straight away. Wake-up takes up to 10 us (tWAKEUP)
	DAC->CR = DAC_CR_EN1;
}
