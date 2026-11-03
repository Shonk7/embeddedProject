#ifndef DAC_H
#define DAC_H

#include "../Headers/stm32f103xe.h"

// Output voltage = code x 3.3 V / 4095. With the buffer on, the pin only reaches
// about 0.2 V to 3.1 V, so codes near 0 or 4095 will not be accurate
#define DAC_MAX_CODE	4095U
#define DAC_MID_CODE	2048U	// 1.65 V, the resting level for audio
#define DAC_VREF_MV		3300U

void dac_init(void);

// With no trigger selected, this reaches the pin one APB1 clock later
__STATIC_INLINE void dac_write(uint32_t code) {
	DAC->DHR12R1 = code;
}

#endif
