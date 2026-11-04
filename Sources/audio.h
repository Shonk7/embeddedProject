#ifndef AUDIO_H
#define AUDIO_H

#include "../Headers/stm32f103xe.h"

// Health stats written by the ISR. The main loop can read them at any time, because
// a 32-bit read can't be interrupted halfway
extern volatile uint32_t audio_overruns;	// edges where the last conversion wasn't done
extern volatile uint32_t audio_late_edges;	// edges that arrived while the ISR was busy
extern volatile uint32_t audio_max_cycles;	// longest ISR so far, in CPU cycles

// Starts the audio path. Samples flow as soon as the external clock runs
void audio_init(void);

#endif
