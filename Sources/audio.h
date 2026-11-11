#ifndef AUDIO_H
#define AUDIO_H

#include "../Headers/stm32f103xe.h"

// Health stats written by the ISR. The main loop can read them at any time, because
// a 32-bit read can't be interrupted halfway
extern volatile uint32_t audio_overruns;	// edges where the last conversion wasn't done
extern volatile uint32_t audio_late_edges;	// edges that arrived while the ISR was busy
extern volatile uint32_t audio_max_cycles;	// longest ISR so far, in CPU cycles

// Optional per-sample processing. 0 (the default) = plain passthrough, ADC code
// straight to the DAC. Sampling/sampling.c installs its function here in
// ACTIVE_TEST 10, and every other test leaves it at 0. The function gets the raw
// ADC code (0 to 4095) and returns the DAC code (0 to 4095)
typedef uint32_t (*audio_process_t)(uint32_t adc_code);
extern audio_process_t volatile audio_process;

// Starts the audio path. Samples flow as soon as the external clock runs
void audio_init(void);

#endif
