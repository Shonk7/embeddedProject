#include "debug.h"

void dbg_init(void) {
	RCC->APB2ENR |= RCC_APB2ENR_IOPFEN;

	// Set the levels first so that the pins do not glitch when they become outputs
	GPIOF->BSRR = GPIO_BSRR_BR0 | GPIO_BSRR_BR7 | GPIO_BSRR_BR8;

	// PF0, PF7: push-pull output, 2 MHz (CNF = 00, MODE = 10). Other pins untouched
	GPIOF->CRL = (GPIOF->CRL & ~(GPIO_CRL_CNF0_Msk | GPIO_CRL_MODE0_Msk |
	                             GPIO_CRL_CNF7_Msk | GPIO_CRL_MODE7_Msk))
	           | (2U << GPIO_CRL_MODE0_Pos) | (2U << GPIO_CRL_MODE7_Pos);

	// PF8: push-pull output, 50 MHz (CNF = 00, MODE = 11) for sharp timing edges
	GPIOF->CRH = (GPIOF->CRH & ~(GPIO_CRH_CNF8_Msk | GPIO_CRH_MODE8_Msk))
	           | (3U << GPIO_CRH_MODE8_Pos);

	// DWT lives behind the debug block, so TRCENA has to be on even with no debugger
	CoreDebug->DEMCR |= CoreDebug_DEMCR_TRCENA_Msk;
	DWT->CYCCNT = 0U;
	DWT->CTRL |= DWT_CTRL_CYCCNTENA_Msk;
}
