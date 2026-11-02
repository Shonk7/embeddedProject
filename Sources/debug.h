#ifndef DEBUG_H
#define DEBUG_H

#include "../Headers/stm32f103xe.h"

// LED0 = PF0 (green), LED7 = PF7 (red), both active high (schematic sheet 10)
// Scope pin = PF8, on header J1101 pin 3. J1101 pin 2 or 8 is GND (sheet 11)

void dbg_init(void);

// BSRR writes are single, atomic stores. Never use GPIOF->ODR ^= ... here: that
// read-modify-write can wipe out a pin the audio ISR changes halfway through it
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
	GPIOF->BSRR = (GPIOF->ODR & GPIO_ODR_ODR0) ? GPIO_BSRR_BR0 : GPIO_BSRR_BS0;
}

__STATIC_INLINE void dbg_fault_led_on(void) {
	GPIOF->BSRR = GPIO_BSRR_BS7;
}

// Free-running CPU cycle counter, wraps every 59.6 s at 72 MHz. Subtract two
// readings as uint32_t and the wrap takes care of itself
__STATIC_INLINE uint32_t dbg_cycles(void) {
	return DWT->CYCCNT;
}

#endif
