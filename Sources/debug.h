#ifndef DEBUG_H
#define DEBUG_H

#include "../Headers/stm32f103xe.h"

// debug.h: small tools for seeing what the firmware is doing from the outside.
//
// The most useful one is the scope pin. The audio ISR drives PF8 high when it
// starts and low when it finishes, so on a scope:
//   - the pulse width is how long the ISR takes (our CPU load)
//   - the pulses should line up one-for-one with the clock edges on PF11. A
//     missing pulse means a lost edge
// The cycle counter does the same job in software: dbg_cycles() before and after
// something tells us how many 72 MHz cycles it took.
//
// Pins:
//   LED5 = PF5 (heartbeat), LED7 = PF7 (red), both on when driven high.
//   PF0 to PF4 belong to the rate LEDs in ui/rate_ctrl.c
//   Scope pin = PF8, on J1101 pin 3. J1101 pin 2 or 8 is GND

// Sets up the LEDs, the scope pin and the cycle counter. main() calls it first
void dbg_init(void);

// Pins change through BSRR, a single atomic write. GPIOF->ODR ^= ... could undo
// a pin change if an interrupt landed halfway through it
// Scope pin high: the audio ISR calls this as its first job
__STATIC_INLINE void dbg_scope_high(void) {
	GPIOF->BSRR = GPIO_BSRR_BS8;
}

// Scope pin low: the audio ISR calls this when its real work is done
__STATIC_INLINE void dbg_scope_low(void) {
	GPIOF->BSRR = GPIO_BSRR_BR8;
}

// Flips the scope pin. Handy for marking a one-off event in test code
__STATIC_INLINE void dbg_scope_toggle(void) {
	GPIOF->BSRR = (GPIOF->ODR & GPIO_ODR_ODR8) ? GPIO_BSRR_BR8 : GPIO_BSRR_BS8;
}

// Flips the heartbeat LED (PF5), so we can see a loop is still running
__STATIC_INLINE void dbg_led_toggle(void) {
	GPIOF->BSRR = (GPIOF->ODR & GPIO_ODR_ODR5) ? GPIO_BSRR_BR5 : GPIO_BSRR_BS5;
}

// Red LED: the clock didn't reach 72 MHz
__STATIC_INLINE void dbg_fault_led_on(void) {
	GPIOF->BSRR = GPIO_BSRR_BS7;
}

// CPU cycle counter, counting up at 72 MHz (72 counts = 1 us). It wraps every
// 59.6 s, but (later - earlier) on two uint32_t readings still gives the right
// answer across the wrap, as long as the gap is under 59.6 s. Used all over the
// firmware for timing without tying up a hardware timer
__STATIC_INLINE uint32_t dbg_cycles(void) {
	return DWT->CYCCNT;
}

#endif
