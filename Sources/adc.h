#ifndef ADC_H
#define ADC_H

#include "../Headers/stm32f103xe.h"

// PA3 is ADC channel 3, even though the schematic names this net ADC2
#define ADC_AUDIO_CHANNEL	3U

// SMP3 code: 0 = 1.5, 1 = 7.5, 2 = 13.5 ADC clocks of sampling. At 12 MHz a
// conversion is (sampling + 12.5) x 6 CPU cycles, so code 1 takes 120 cycles
#define ADC_SAMPLE_TIME		1U

void adc_init(void);

// EXTTRIG and EXTSEL = SWSTART are set in adc_init, so this starts one conversion
__STATIC_INLINE void adc_start(void) {
	ADC1->CR2 |= ADC_CR2_SWSTART;
}

__STATIC_INLINE int adc_is_done(void) {
	return (ADC1->SR & ADC_SR_EOC) != 0U;
}

// Reading DR also clears EOC
__STATIC_INLINE uint32_t adc_result(void) {
	return ADC1->DR;
}

#endif
