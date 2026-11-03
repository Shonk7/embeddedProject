#ifndef DAC_H
#define DAC_H

#include "../Headers/stm32f103xe.h"

// Output voltage = code x 3.3 V / 4095. The pin only really reaches 0.2 V to 3.1 V,
// so codes near 0 or 4095 won't be accurate
#define DAC_MAX_CODE	4095U
#define DAC_MID_CODE	2048U	// 1.65 V, the resting level for audio
#define DAC_VREF_MV		3300U

void dac_init(void);

// Sends a 12-bit value to PA4. It reaches the pin almost straight away
__STATIC_INLINE void dac_write(uint32_t code) {
	DAC->DHR12R1 = code;
}

#endif
