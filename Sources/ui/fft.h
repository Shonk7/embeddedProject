#ifndef FFT_H
#define FFT_H

#include <stdint.h>

// Ext B.1: 128-point FFT of one capture block (Sources/capture.h).
//
// Each frame: remove the block's average, apply a Hann window, run a 128-point
// radix-2 FFT, then turn each bin into a level in dB. Main loop only. The ISR
// never calls any of this.
//
// A real input gives 128 complex bins, but bins 65 to 127 mirror bins 63 to 1,
// so only bins 0 to 64 (0 Hz to half the capture rate) carry new information.
// At a 49.9 kHz capture rate each bin is 390 Hz wide, and 0 to 20 kHz is bins
// 0 to 51

#define FFT_N			128U
#define FFT_LOG2_N		7U
#define FFT_HALF		(FFT_N / 2U)	// bins 0 to 63 are returned

// Levels are in tenths of a dB, relative to a full-scale sine (0 = the loudest
// sine the ADC can take, -200 = 20 dB below that). Anything quieter than
// FFT_FLOOR_DB10 is reported as FFT_FLOOR_DB10
#define FFT_FLOOR_DB10	(-1000)

// Builds the twiddle, window and bit-reverse tables. Uses double maths, so call
// once at start-up, not per frame
void fft_init(void);

// in = 128 capture points (0 to 32760, silence 16384). Writes the level of bins
// 0 to 63 to db10_out, in tenths of a dB
void fft_spectrum(const uint16_t *in, int32_t *db10_out);

#endif
