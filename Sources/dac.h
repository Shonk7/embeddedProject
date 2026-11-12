#ifndef DAC_H
#define DAC_H

#include "../Headers/stm32f103xe.h"

// dac.h: the audio output side.
//
// The DAC turns a 12-bit number back into a voltage on PA4, which goes to J1301
// and on to the board's amplifier and speaker. The audio ISR writes one new value
// per clock edge, so the output is a staircase that steps once per sample (a
// "zero-order hold"). Smoothing those steps, the reconstruction filter the spec
// asks about, has to be done in analog after J1301: firmware can't do it.

// Output voltage = code x 3.3 V / 4095. The pin only really reaches 0.2 V to 3.1 V,
// so codes near 0 or 4095 won't be accurate
#define DAC_MAX_CODE	4095U
#define DAC_MID_CODE	2048U	// 1.65 V, the resting level for audio
#define DAC_VREF_MV		3300U

// Sets up DAC channel 1 on PA4, resting at mid-scale. audio_init() calls this
void dac_init(void);

// Sends a 12-bit value to PA4. It reaches the pin almost straight away
__STATIC_INLINE void dac_write(uint32_t code) {
	DAC->DHR12R1 = code;
}

#endif
