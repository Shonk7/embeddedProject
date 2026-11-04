#include "debug.h"

// Sets up the LEDs, the scope pin and the cycle counter
void dbg_init(void) {
	RCC->APB2ENR |= RCC_APB2ENR_IOPFEN;

	// Start all three pins low, so they don't glitch when they become outputs
	GPIOF->BSRR = GPIO_BSRR_BR0 | GPIO_BSRR_BR7 | GPIO_BSRR_BR8;

	// LEDs: push-pull output at 2 MHz (CNF = 00, MODE = 10). Other pins stay as they are
	GPIOF->CRL = (GPIOF->CRL & ~(GPIO_CRL_CNF0_Msk | GPIO_CRL_MODE0_Msk |
	                             GPIO_CRL_CNF7_Msk | GPIO_CRL_MODE7_Msk))
	           | (2U << GPIO_CRL_MODE0_Pos) | (2U << GPIO_CRL_MODE7_Pos);

	// Scope pin: push-pull output at 50 MHz (MODE = 11), for sharp timing edges
	GPIOF->CRH = (GPIOF->CRH & ~(GPIO_CRH_CNF8_Msk | GPIO_CRH_MODE8_Msk))
	           | (3U << GPIO_CRH_MODE8_Pos);

	// Start the cycle counter. It sits in the debug block, so TRCENA has to be on
	// even with no debugger attached
	CoreDebug->DEMCR |= CoreDebug_DEMCR_TRCENA_Msk;
	DWT->CYCCNT = 0U;
	DWT->CTRL |= DWT_CTRL_CYCCNTENA_Msk;
}
