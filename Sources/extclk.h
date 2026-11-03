#ifndef EXTCLK_H
#define EXTCLK_H

#include "../Headers/stm32f103xe.h"

// External sample clock, coming in on PF11 (J1101 pin 6)

// Rising edges counted so far. Only the ISR writes it, and main can safely read
// it because a 32-bit read can't be interrupted halfway
extern volatile uint32_t extclk_edges;

void extclk_init(void);

#endif
