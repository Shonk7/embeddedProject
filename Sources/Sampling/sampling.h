#ifndef SAMPLING_H
#define SAMPLING_H

#include <stdint.h>

// The step between the ADC and the DAC for every sample (installed at start-up).
// audio.c still does the sampling itself: the external clock edge fires the ISR,
// which starts the ADC. This module takes each sample the ISR collects and:
//   1. counts samples that hit the ADC's limits (input too loud: clipping)
//   2. removes the 2048 offset and any DC, giving a signed sample
//   3. runs the 3-band mixer, but only while the mixer is on (fx/8)
//   4. saturates and adds the offset back, ready for the DAC
// At the other three rates it hands the ADC code straight back (passthrough), so
// the fast rates keep their full cycle budget
//
// Anti-aliasing can't happen here. Once the ADC has sampled, anything above half
// the sample rate has already folded down on top of the real signal. It needs an
// analog low-pass filter before the audio jack

// Call after audio_init(). Plugs sampling_process() into the audio ISR
void sampling_init(void);

// 1 if sampling_init() has run, so the UI can say whether the mixer path is
// installed
int sampling_is_running(void);

// ADC samples within SAMPLING_CLIP_MARGIN codes of 0 or 4095, counted while the
// mixer runs. A rising count means the input is too loud
extern volatile uint32_t sampling_clips;

#endif
