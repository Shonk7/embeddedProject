#ifndef ADC_H
#define ADC_H

#include "../Headers/stm32f103xe.h"
#include <stdbool.h>

// The audio jack reaches PA3, which is channel 3. The schematic calls this net ADC2
#define ADC_AUDIO_CHANNEL	3U

// How long the ADC samples: 0 = 1.5, 1 = 7.5, 2 = 13.5 ADC clocks. A conversion
// costs (sampling + 12.5) x 6 CPU cycles, so 1 gives 120 cycles out of our 180
#define ADC_SAMPLE_TIME		1U

void adc_init(void);

// Starts one conversion. It finishes in the background while the CPU carries on
__STATIC_INLINE void adc_start(void) {
	ADC1->CR2 |= ADC_CR2_SWSTART;
}

// True once the conversion has finished
__STATIC_INLINE bool adc_is_done(void) {
	return (ADC1->SR & ADC_SR_EOC) != 0U;
}

// Gets the result, 0 to 4095. Reading it also clears the done flag
__STATIC_INLINE uint32_t adc_result(void) {
	return ADC1->DR;
}

#endif
