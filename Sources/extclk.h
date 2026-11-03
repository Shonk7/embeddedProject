#ifndef EXTCLK_H
#define EXTCLK_H

#include "../Headers/stm32f103xe.h"

// External sample clock in on PF11 (J1101 pin 6), EXTI line 11

// Rising edges seen so far. Only the ISR writes this, and an aligned 32-bit read
// is atomic, so main can read it without turning interrupts off
extern volatile uint32_t extclk_edges;

void extclk_init(void);

#endif
