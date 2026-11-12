#include <math.h>
#include "../../Headers/stm32f103xe.h"
#include "fft.h"

// fft.c: a plain integer FFT, written for this chip (no float hardware). Each
// frame goes through four steps, all in fft_spectrum():
//   1. Remove the block's average (DC), so it doesn't hide the low bins
//   2. Multiply by a Hann window, so the block's hard edges don't smear the bins
//   3. The FFT itself: 7 stages of "butterflies" (128 = 2^7)
//   4. Turn each bin's size into decibels, so quiet and loud parts both show
// The tables it needs (sines, cosines, window, bit-reversal order) are worked out
// once in fft_init(), so the per-frame work is only integer adds, multiplies and
// shifts.

#define PI			3.14159265358979323846

// Twiddles are Q30 (1.0 = 2^30). Data is plain int32 with no scaling between
// stages: the input is at most +-16380 (15 bits), and 7 stages can grow it by
// 128 (7 bits), so the data never goes past 22 bits. Each twiddle product is done
// in 64 bits (SMULL on the M3), so nothing overflows and nothing is thrown away
#define TW_BITS		30U
#define WIN_BITS	15U		// window is Q15 (1.0 = 32768)

// cos and sin of 2 pi m / 128, for m = 0 to 63. These "twiddle factors" are the
// rotations the butterflies multiply by. Every stage needs a subset of them
static int32_t tw_cos[FFT_HALF];
static int32_t tw_sin[FFT_HALF];

// Hann window, 0.5 x (1 - cos(2 pi i / 128))
static uint16_t window[FFT_N];

// Where each input point goes so the butterflies can run in place. This FFT
// splits the data into even and odd samples over and over, and the order that
// produces is the input index with its 7 bits written backwards
static uint8_t bit_rev[FFT_N];

// Working data for one frame: real and imaginary part of each point. They're
// static, not on the stack, because 1 KB of locals is a lot for this chip
static int32_t re[FFT_N];
static int32_t im[FFT_N];

// log2 of the power a full-scale sine gives, in Q8 (x 256). A sine of amplitude
// A through a Hann window peaks at A x 128 / 2 x 0.5 = 32 A. Full scale is
// A = 16380, so the peak is 524160 and its power is 524160^2 = 2^37.999
#define FULL_SCALE_LOG2_Q8	9728

// Tenths of a dB per Q8 log2 step, as a Q16 multiplier: 10 x log10(2) x 10 / 256
// = 0.11759, x 65536 = 7706
#define DB10_PER_LOG2_Q8_Q16	7706

// Builds the three lookup tables. Slow (double maths), so it runs only once
void fft_init(void) {
	uint32_t i;
	uint32_t b;
	uint32_t r;

	// Twiddles, rounded to the nearest Q30 integer
	for (i = 0U; i < FFT_HALF; i++) {
		tw_cos[i] = (int32_t)floor(cos(2.0 * PI * (double)i / (double)FFT_N) * (double)(1UL << TW_BITS) + 0.5);
		tw_sin[i] = (int32_t)floor(sin(2.0 * PI * (double)i / (double)FFT_N) * (double)(1UL << TW_BITS) + 0.5);
	}

	// Periodic Hann (divides by N, not N - 1), the usual choice in front of an FFT
	for (i = 0U; i < FFT_N; i++) {
		window[i] = (uint16_t)floor(0.5 * (1.0 - cos(2.0 * PI * (double)i / (double)FFT_N)) *
		                            (double)(1UL << WIN_BITS) + 0.5);
	}

	// Reverse the 7 index bits: 1 (0000001) goes to 64 (1000000), and so on
	for (i = 0U; i < FFT_N; i++) {
		r = 0U;
		for (b = 0U; b < FFT_LOG2_N; b++) {
			if (i & (1U << b)) {
				r |= 1U << (FFT_LOG2_N - 1U - b);
			}
		}
		bit_rev[i] = (uint8_t)r;
	}
}

// log2(p) in Q8, using the top bit's position plus the next 8 bits as the fraction.
// The straight-line fraction is at most 0.086 out, which is under 0.3 dB.
// Example: p = 3000 is binary 1011 1011 1000, top bit 11, next 8 bits 0111 0111
// = 119, so log2 = 11 + 119/256 = 11.46 (the exact answer is 11.55). __CLZ counts
// leading zeros in one instruction, which is how we find the top bit quickly
static uint32_t log2_q8(uint64_t p) {
	uint32_t hi = (uint32_t)(p >> 32);
	uint32_t msb;
	uint32_t frac;

	if (p == 0U) {
		return 0U;
	}
	msb = (hi != 0U) ? (63U - __CLZ(hi)) : (31U - __CLZ((uint32_t)p));
	if (msb >= 8U) {
		frac = (uint32_t)(p >> (msb - 8U)) & 0xFFU;
	} else {
		frac = ((uint32_t)p << (8U - msb)) & 0xFFU;
	}
	return msb * 256U + frac;
}

// One frame: 128 capture points in, 64 levels in tenths of a dB out
void fft_spectrum(const uint16_t *in, int32_t *db10_out) {
	uint32_t i;
	uint32_t j;
	uint32_t k;
	uint32_t size;
	uint32_t half;
	uint32_t step;
	uint32_t start;
	int32_t sum = 0;
	int32_t mean;
	int32_t x;
	int32_t wr;
	int32_t ws;
	int32_t tr;
	int32_t ti;
	int32_t db10;
	uint64_t power;

	// Take out the block's average. The capture is centred on 16384, but the
	// real resting level drifts a little, and leftover DC would swamp bins 0 and 1
	for (i = 0U; i < FFT_N; i++) {
		sum += (int32_t)in[i];
	}
	mean = sum / (int32_t)FFT_N;

	// Window, and load in bit-reversed order. The window fades both ends of the
	// block to 0, so a tone that doesn't fit a whole number of cycles doesn't
	// smear across every bin
	for (i = 0U; i < FFT_N; i++) {
		x = (((int32_t)in[i] - mean) * (int32_t)window[i]) >> WIN_BITS;
		re[bit_rev[i]] = x;
		im[bit_rev[i]] = 0;
	}

	// Radix-2 decimation in time: 7 stages of 64 butterflies. Each stage joins
	// pairs of half-size FFTs. W = cos - j sin of 2 pi k / size
	// Stage 1 joins 64 pairs of 1-point FFTs into 2-point ones, stage 2 makes
	// 4-point ones, and so on until stage 7 gives the full 128-point result.
	// A butterfly takes two points a and b and replaces them with a + W b and
	// a - W b, which is where the name comes from if you draw it
	for (size = 2U; size <= FFT_N; size <<= 1) {
		half = size >> 1;
		step = FFT_N / size;
		for (start = 0U; start < FFT_N; start += size) {
			for (k = 0U; k < half; k++) {
				i = start + k;
				j = i + half;
				wr = tw_cos[k * step];
				ws = tw_sin[k * step];

				// W b = (re + j im) x (wr - j ws), done in 64 bits then scaled back
				tr =(int32_t)((((int64_t)wr * re[j]) + ((int64_t)ws * im[j])) >> TW_BITS);
				ti = (int32_t)((((int64_t)wr * im[j]) - ((int64_t)ws * re[j])) >> TW_BITS);

				// b becomes a - W b, then a becomes a + W b
				re[j] = re[i] - tr;
				im[j] = im[i] - ti;
				re[i] += tr;
				im[i] += ti;
			}
		}
	}

	// Power of each bin, then dB relative to a full-scale sine:
	// dB = 10 log10(P / P_full) = 3.0103 x (log2 P - log2 P_full)
	// Power = re^2 + im^2 (the size squared, so no square root is needed). A
	// full-scale sine reads 0 dB, half that amplitude about -6 dB, and silence
	// sits at the floor
	for (i = 0U; i < FFT_HALF; i++) {
		power = (uint64_t)((int64_t)re[i] * re[i]) + (uint64_t)((int64_t)im[i] * im[i]);
		db10 = (((int32_t)log2_q8(power) - FULL_SCALE_LOG2_Q8) * DB10_PER_LOG2_Q8_Q16) >> 16;
		if ((power == 0U) || (db10 < FFT_FLOOR_DB10)) {
			db10 = FFT_FLOOR_DB10;
		}
		db10_out[i] = db10;
	}
}
