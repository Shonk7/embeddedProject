#ifndef MIXER_H
#define MIXER_H

#include <stdint.h>
#include <stdbool.h>

// Extension A: 3-band audio mixer. Design and test bench: mixer_design.m
//
//   low  = LP(fc1) -> LP(fc1)
//   mid  = HP(fc1) -> HP(fc1) -> LP(fc2) -> LP(fc2)
//   high = HP(fc2) -> HP(fc2)
//   out  = g_low * low + g_mid * mid + g_high * high
//
// Each LP/HP is a 2nd-order Butterworth biquad. Two in series make a 4th-order
// Linkwitz-Riley filter, so with equal gains the bands add back to a flat response.
// All the per-sample maths is integer (no float in the ISR). The arithmetic is the
// same, bit for bit, as mixerFixed() in mixer_design.m

#define MIXER_BANDS			3U
#define MIXER_LOW			0U
#define MIXER_MID			1U
#define MIXER_HIGH			2U

// Slider gain as an integer: 1.0 = 16384 (Q14), so 0 to 2.0 is 0 to 32768
#define MIXER_GAIN_ONE		16384U
#define MIXER_GAIN_MAX		32768U

// Crossovers (Hz). Default to the spec's 500 Hz and 4 kHz. The mixer page's -/+
// buttons move them within +-50 % (soft goal); mixer_set_crossovers() rejects
// anything outside MIN/MAX
#define MIXER_FC1_DEFAULT	500U
#define MIXER_FC1_MIN		250U
#define MIXER_FC1_MAX		750U
#define MIXER_FC2_DEFAULT	4000U
#define MIXER_FC2_MIN		2000U
#define MIXER_FC2_MAX		6000U

// mixer_process() takes the sample shifted left by this many bits. The extra bits
// keep rounding errors 256 times below one ADC count (mixer_design.m, STATE_BITS)
#define MIXER_STATE_BITS	8U

// 1 = the ISR should run the mixer. Only main writes it (mixer_set_active)
extern volatile uint32_t mixer_on;

// ---- Main loop only -------------------------------------------------------
// Default crossovers, gains of 1.0, mixer off. fs_hz = sample rate the
// coefficients are designed for
void mixer_init(uint32_t fs_hz);

// Works out new coefficients (slow, uses double maths) and swaps them in at once.
// Returns false and changes nothing if a value is out of range
bool mixer_set_crossovers(uint32_t fc1_hz, uint32_t fc2_hz, uint32_t fs_hz);

void mixer_set_gain(uint32_t band, uint32_t gain_q14);
uint32_t mixer_get_gain(uint32_t band);
uint32_t mixer_fc1(void);
uint32_t mixer_fc2(void);
uint32_t mixer_fs(void);

// On at fx/8 only. Turning it on clears the filters' memory first
void mixer_set_active(int on);

static inline bool mixer_is_active(void) {
	return mixer_on != 0U;
}

// ---- ISR only ---------------------------------------------------------------
// x = (sample - DC) << MIXER_STATE_BITS. Returns the mixed sample in ADC counts,
// -2048 to 2047 (saturated)
int32_t mixer_process(int32_t x);

#endif
