#ifndef CAPTURE_H
#define CAPTURE_H

#include "../Headers/stm32f103xe.h"

// capture.h: Stage 7, which hands blocks of input samples from the audio ISR to
// the FFT on the spectrum page (Ext B.1).
//
// Who uses what:
//   audio.c (ISR)  calls capture_push() with every raw input sample, but only
//                  while capture_armed is 1
//   ui/ui.c        calls capture_start(), capture_ready(), capture_data() and
//                  capture_stop() from the spectrum page (main loop)
//   ui/fft.c       takes the 128 points from capture_data() and does the maths
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
//   capture_stop() when the page closes
// While a block is ready, the ISR doesn't touch the buffer, so the FFT can read it
// at its own pace. Nothing is copied. We miss the samples that arrive between
// blocks, but a spectrum display only needs a fresh snapshot each frame
//
// Example at fx (399.25 kHz): the ISR adds up 8 samples, stores the sum as one
// point, and repeats. 128 points x 8 = 1024 samples = 2.6 ms of audio, and the
// points come out 8 / 399250 s apart, i.e. at 49.9 kHz

// Points per block: one FFT's worth (ui/fft.h FFT_N)
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

// 1 once all 128 points are in. Takes about 2.6 ms after capture_start() at any
// rate. Never 1 if the clock is stopped, so callers should give up after a while
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
