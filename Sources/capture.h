#ifndef CAPTURE_H
#define CAPTURE_H

#include "../Headers/stm32f103xe.h"

// Stage 7: grabs blocks of input samples for the FFT (Ext B).
//
// The FFT is 128 points at all four rates, and it has to show 0 to 20 kHz. At fx
// a plain 128-point block would only give 3.1 kHz per bin, about 6 bins in the
// range we care about. So the ISR adds up 8, 4, 2 or 1 samples before storing one,
// which brings every rate down to about fx/8 = 49.9 kHz, and gives 390 Hz bins
// at every rate.
//
// How to use it (main loop only):
//   capture_start(measured_rate_hz);
//   ... later ...
//   if (capture_ready()) { use capture_data(); then capture_start() again }
// While a block is ready, the ISR doesn't touch the buffer, so the FFT can read it
// at its own pace. Nothing is copied

#define CAPTURE_POINTS		128U

// Every stored point is scaled to 8 ADC codes' worth, whatever the rate, so the
// FFT sees the same numbers at all four rates. Range 0 to 32760, silence = 16384
#define CAPTURE_FULL_SCALE	(4095U * 8U)
#define CAPTURE_MID			(2048U * 8U)

// Halfway (on a log scale) between neighbouring rates: fx/8 x sqrt(2) = 70577 Hz.
// A measured rate above SPLIT x 4 gets 8 samples per point, above SPLIT x 2 gets 4,
// above SPLIT gets 2, and anything slower gets 1
#define CAPTURE_SPLIT_HZ	70577U

// 1 while the ISR is filling the buffer. The ISR checks this on every sample, so
// an idle capture costs one load and one branch
extern volatile uint32_t capture_armed;

// Starts a new block. rate_hz is the measured sample rate, which picks how many
// samples go into each point. Main loop only
void capture_start(uint32_t rate_hz);

// Stops a block part-way and forgets any finished one. Called when the spectrum
// page closes, so the ISR does no capture work while the page isn't shown.
// Main loop only
void capture_stop(void);

// 1 once all 128 points are in
int capture_ready(void);

// The block, oldest point first. Only valid while capture_ready() is 1
const uint16_t *capture_data(void);

// Samples added up per point in the last block (1, 2, 4 or 8)
uint32_t capture_decimation(void);

// CPU cycles from the first point to the last (127 point periods). Gives the real
// capture rate: 127 x 72 MHz / this, which should be about 49.9 kHz at every rate
uint32_t capture_span_cycles(void);

// ISR only. code = raw ADC code, 0 to 4095
void capture_push(uint32_t code);

#endif
