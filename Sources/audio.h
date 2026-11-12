#ifndef AUDIO_H
#define AUDIO_H

#include "../Headers/stm32f103xe.h"

// audio.h: the core audio path, jack -> ADC -> (processing) -> DAC -> speaker.
//
// Everything happens in one interrupt, EXTI15_10_IRQHandler in audio.c, which
// fires on every rising edge of the clock board's signal. The main loop never
// touches the audio samples. It only reads the stats below and can swap in a
// processing function.
//
// CPU cycles available per sample at 72 MHz (the ISR must finish well inside these):
//   fx   399.25 kHz   180 cycles
//   fx/2 199.6  kHz   361 cycles
//   fx/4  99.8  kHz   721 cycles
//   fx/8  49.9  kHz  1443 cycles  <- the only rate the mixer runs at

// Health stats written by the ISR. They're how we prove to the marker that no
// samples are dropped or repeated. The main loop can read them at any time,
// because a 32-bit read can't be interrupted halfway
extern volatile uint32_t audio_overruns;	// edges where the last conversion wasn't done
											// (that sample gets repeated)
extern volatile uint32_t audio_late_edges;	// edges that arrived while the ISR was busy
											// (two of these in a row would lose an edge)
extern volatile uint32_t audio_max_cycles;	// longest ISR so far, in CPU cycles. ui.c
											// reads and clears it every half second

// Optional per-sample processing. 0 (the default) = plain passthrough, ADC code
// straight to the DAC. Sampling/sampling.c installs its function here at
// start-up (sampling_init in main.c). The function gets the raw
// ADC code (0 to 4095) and returns the DAC code (0 to 4095). It runs inside the
// ISR, so it must be fast and must never wait for anything
typedef uint32_t (*audio_process_t)(uint32_t adc_code);
extern audio_process_t volatile audio_process;

// Starts the audio path. Samples flow as soon as the external clock runs
void audio_init(void);

#endif
