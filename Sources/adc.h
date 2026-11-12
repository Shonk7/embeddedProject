#ifndef ADC_H
#define ADC_H

#include "../Headers/stm32f103xe.h"

// adc.h: the audio input side.
//
// Audio from the jack (J1302), biased around 1.65 V by the board's analog stage,
// reaches pin PA3. The ADC turns that voltage into a 12-bit number, 0 to 4095,
// with silence at about 2048.
//
// The audio ISR uses it like a relay race: on each clock edge it collects the
// result started on the previous edge, then starts the next conversion straight
// away. The conversion runs in the background between edges, so the ISR never
// sits waiting for the ADC. The moment adc_start() is called is our sampling
// instant, so it's always at the same short delay after the clock edge.

// The audio jack reaches PA3, which is channel 3. The schematic calls this net ADC2
#define ADC_AUDIO_CHANNEL	3U

// How long the ADC samples: 0 = 1.5, 1 = 7.5, 2 = 13.5 ADC clocks. A conversion
// costs (sampling + 12.5) x 6 CPU cycles, so 1 gives 120 cycles out of our 180
#define ADC_SAMPLE_TIME		1U

// Sets up ADC1 on PA3 and calibrates it. audio_init() calls this
void adc_init(void);

// Starts one conversion. It finishes in the background while the CPU carries on
__STATIC_INLINE void adc_start(void) {
	ADC1->CR2 |= ADC_CR2_SWSTART;
}

// True once the conversion has finished
__STATIC_INLINE int adc_is_done(void) {
	return (ADC1->SR & ADC_SR_EOC) != 0U;
}

// Gets the result, 0 to 4095. Reading it also clears the done flag
__STATIC_INLINE uint32_t adc_result(void) {
	return ADC1->DR;
}

#endif
