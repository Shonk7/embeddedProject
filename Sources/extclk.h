#ifndef EXTCLK_H
#define EXTCLK_H

#include "../Headers/stm32f103xe.h"

// extclk.h: the link between our clock board and the audio interrupt.
//
// The spec says sample timing must come from our own hardware, not from a timer
// inside the STM32. So the clock board's square wave goes into a plain GPIO pin,
// and the chip's EXTI ("external interrupt") block fires an interrupt on every
// rising edge. One edge = one sample. Changing the clock board's rate changes
// the sample rate, with no firmware change needed.
//
// External sample clock from the clock board, coming in on PF11 (J1101 pin 6,
// through the board's 330 ohm R_OUT). Rising edges fire EXTI15_10_IRQHandler,
// which is in audio.c. The board docs specified J1104 pin 7 (PG1); the team
// moved it to J1101 so both clock-board strobes sit on one header

// Pending flag for the clock's EXTI line, used by the ISR
#define EXTCLK_PR_FLAG	EXTI_PR_PR11

// Rising edges counted so far. Only the ISR writes it, and main can safely read
// it because a 32-bit read can't be interrupted halfway. This is how we measure
// the real sample rate: count how much it goes up over a known number of CPU
// cycles (stats_update() in ui/ui.c does exactly that every half second)
extern volatile uint32_t extclk_edges;

// Routes PF11 to the interrupt and switches it on. audio_init() calls this last,
// once the ADC and DAC are ready for the first edge
void extclk_init(void);

#endif
