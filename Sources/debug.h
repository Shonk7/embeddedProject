#ifndef DEBUG_H
#define DEBUG_H

#include "../Headers/stm32f103xe.h"

// Pins for watching what the firmware is doing:
//   LED5 = PF5 (heartbeat), LED7 = PF7 (red), both on when driven high.
//   PF0 to PF4 belong to the rate LEDs in ui/rate_ctrl.c
//   Scope pin = PF8, on J1101 pin 3. J1101 pin 2 or 8 is GND

void dbg_init(void);

// Pins change through BSRR, a single atomic write. GPIOF->ODR ^= ... could undo
// a pin change if an interrupt landed halfway through it
__STATIC_INLINE void dbg_scope_high(void) {
	GPIOF->BSRR = GPIO_BSRR_BS8;
}

__STATIC_INLINE void dbg_scope_low(void) {
	GPIOF->BSRR = GPIO_BSRR_BR8;
}

__STATIC_INLINE void dbg_scope_toggle(void) {
	GPIOF->BSRR = (GPIOF->ODR & GPIO_ODR_ODR8) ? GPIO_BSRR_BR8 : GPIO_BSRR_BS8;
}

__STATIC_INLINE void dbg_led_toggle(void) {
	GPIOF->BSRR = (GPIOF->ODR & GPIO_ODR_ODR5) ? GPIO_BSRR_BR5 : GPIO_BSRR_BS5;
}

// Red LED: the clock didn't reach 72 MHz
__STATIC_INLINE void dbg_fault_led_on(void) {
	GPIOF->BSRR = GPIO_BSRR_BS7;
}

// CPU cycle counter. It wraps every 59.6 s at 72 MHz, but subtracting two
// uint32_t readings still gives the right answer
__STATIC_INLINE uint32_t dbg_cycles(void) {
	return DWT->CYCCNT;
}

#endif
