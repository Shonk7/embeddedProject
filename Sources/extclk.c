#include "extclk.h"
#include "debug.h"

volatile uint32_t extclk_edges = 0U;

void extclk_init(void) {
	// AFIO holds the pin routing. Without its clock the EXTICR write below is
	// lost, and line 11 stays on its default pin, PA11
	RCC->APB2ENR |= RCC_APB2ENR_IOPFEN | RCC_APB2ENR_AFIOEN;

	// PF11 input with pull-down (CNF = 10, MODE = 00, ODR = 0), so an unplugged
	// wire reads low rather than picking up noise and firing interrupts
	GPIOF->BSRR = GPIO_BSRR_BR11;
	GPIOF->CRH = (GPIOF->CRH & ~(GPIO_CRH_CNF11_Msk | GPIO_CRH_MODE11_Msk))
	           | (2U << GPIO_CRH_CNF11_Pos);

	// Connect PF11 to EXTI line 11. EXTICR[2] is the manual's AFIO_EXTICR3
	AFIO->EXTICR[2] = (AFIO->EXTICR[2] & ~AFIO_EXTICR3_EXTI11_Msk) | AFIO_EXTICR3_EXTI11_PF;

	// Rising edges only, so one interrupt per clock period
	EXTI->RTSR |= EXTI_RTSR_TR11;
	EXTI->FTSR &= ~EXTI_FTSR_TR11;

	// Drop anything latched during setup, then let line 11 through
	EXTI->PR = EXTI_PR_PR11;
	EXTI->IMR |= EXTI_IMR_MR11;

	// Priority 0 is the highest, so nothing else can delay a sample. Lines 10
	// to 15 share this vector, but only line 11 is unmasked
	NVIC_ClearPendingIRQ(EXTI15_10_IRQn);
	NVIC_SetPriority(EXTI15_10_IRQn, 0U);
	NVIC_EnableIRQ(EXTI15_10_IRQn);
}

// This exact name replaces the weak default in the startup file's vector table
void EXTI15_10_IRQHandler(void) {
	dbg_scope_high();				// PF8 is high while the ISR runs

	// Pending bits clear by writing 1. Left set, this ISR would fire forever
	EXTI->PR = EXTI_PR_PR11;
	extclk_edges++;

	dbg_scope_low();
}
