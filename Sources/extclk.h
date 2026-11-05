#ifndef EXTCLK_H
#define EXTCLK_H

#include "../Headers/stm32f103xe.h"

// External sample clock from the clock board, coming in on PF11 (J1101 pin 6,
// through the board's 330 ohm R_OUT). Rising edges fire EXTI15_10_IRQHandler,
// which is in audio.c. The board docs specified J1104 pin 7 (PG1); the team
// moved it to J1101 so both clock-board strobes sit on one header

// Pending flag for the clock's EXTI line, used by the ISR
#define EXTCLK_PR_FLAG	EXTI_PR_PR11

// Rising edges counted so far. Only the ISR writes it, and main can safely read
// it because a 32-bit read can't be interrupted halfway
extern volatile uint32_t extclk_edges;

void extclk_init(void);

#endif
