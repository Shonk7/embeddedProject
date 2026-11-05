#include "extclk.h"

volatile uint32_t extclk_edges = 0U;

// Makes every rising edge on PF11 (J1101 pin 6) fire EXTI15_10_IRQHandler,
// which is in audio.c
void extclk_init(void) {
	// AFIO does the pin routing below. Without its clock, that write is ignored
	RCC->APB2ENR |= RCC_APB2ENR_IOPFEN | RCC_APB2ENR_AFIOEN;

	// PF11 is an input with pull-down (CNF = 10, ODR = 0), so an unplugged wire
	// sits low instead of firing random interrupts. Pins 8-15 are in CRH
	GPIOF->BSRR = GPIO_BSRR_BR11;
	GPIOF->CRH = (GPIOF->CRH & ~(GPIO_CRH_CNF11_Msk | GPIO_CRH_MODE11_Msk))
	           | (2U << GPIO_CRH_CNF11_Pos);

	// Connect PF11 to EXTI line 11. EXTICR[2] is the manual's AFIO_EXTICR3
	AFIO->EXTICR[2] = (AFIO->EXTICR[2] & ~AFIO_EXTICR3_EXTI11_Msk) | AFIO_EXTICR3_EXTI11_PF;

	// Trigger on rising edges only, so we get one interrupt per clock period
	EXTI->RTSR |= EXTI_RTSR_TR11;
	EXTI->FTSR &= ~EXTI_FTSR_TR11;

	// Clear anything left over from setup, then let line 11 through
	EXTI->PR = EXTI_PR_PR11;
	EXTI->IMR |= EXTI_IMR_MR11;

	// Turn the interrupt on at priority 0, the highest, so nothing can delay a
	// sample. Lines 10-15 share this vector; only line 11 is enabled, so every
	// entry is a clock edge
	NVIC_ClearPendingIRQ(EXTI15_10_IRQn);
	NVIC_SetPriority(EXTI15_10_IRQn, 0U);
	NVIC_EnableIRQ(EXTI15_10_IRQn);
}
