#include "../audio.h"
#include "../dac.h"
#include "../ui/mixer.h"
#include "sampling.h"

// sampling.c: runs inside the audio ISR on every sample. At fx/8 it turns the raw
// ADC code into a clean signed sample, runs the 3-band mixer, and turns the result
// back into a DAC code. At every other rate it returns the sample untouched.
// See sampling.h for the steps.

// The ADC's resting code. The input sits at half the 3.3 V supply
#define ADC_MID_CODE		2048

// A sample this close to 0 or 4095 counts as clipped
#define SAMPLING_CLIP_MARGIN	16U
#define CLIP_LOW			SAMPLING_CLIP_MARGIN
#define CLIP_HIGH			(4095U - SAMPLING_CLIP_MARGIN)

// DC tracking. dc_sum holds the input's running average x 4096, and follows it
// with a time constant of 4096 samples (82 ms at fx/8, a high-pass at about 2 Hz).
// Removing DC matters because the low band has gain up to 2: without this, moving
// the low slider would also move the output's DC level and click
#define DC_SHIFT			12U

// Clipped-sample counter, shown as CLIP on the mixer page
volatile uint32_t sampling_clips = 0U;

// Running average of the input, x 4096. Only the ISR updates it
static int32_t dc_sum = 0;

// Runs inside the audio ISR for every sample. code = raw ADC code (0 to 4095),
// returns the DAC code (0 to 4095)
static uint32_t sampling_process(uint32_t code) {
	int32_t x;
	int32_t x_scaled;
	int32_t y;

	// Faster than fx/8: no mixer, plain passthrough
	if (!mixer_is_active()) {
		return code;
	}

	// 1. Clipping check
	if ((code < CLIP_LOW) || (code > CLIP_HIGH)) {
		sampling_clips++;
	}

	// 2. Signed sample, then remove DC. dc_sum >> 4 is the average in the mixer's
	// format (x 256), so the subtraction keeps the 8 extra fraction bits.
	// The middle line is a leaky average: each sample nudges dc_sum 1/4096 of the
	// way towards x, so it follows slow drift but ignores the audio itself
	x = (int32_t)code - ADC_MID_CODE;
	dc_sum += x - (dc_sum >> DC_SHIFT);
	x_scaled = x * (1 << MIXER_STATE_BITS) - (dc_sum >> (DC_SHIFT - MIXER_STATE_BITS));

	// 3. Three-band mixer. Returns -2048 to 2047, already saturated
	y = mixer_process(x_scaled);

	// 4. Back to the DAC's 0 to 4095 range
	return (uint32_t)(y + (int32_t)DAC_MID_CODE);
}

// Resets the DC tracker and clip count, then plugs sampling_process() into the
// audio ISR. main.c calls this once, straight after audio_init()
void sampling_init(void) {
	dc_sum = 0;
	sampling_clips = 0U;
	audio_process = sampling_process;	// one aligned write: the ISR sees all or nothing
}

// 1 if our function is the one in the ISR's hook (the mixer page checks this)
int sampling_is_running(void) {
	return audio_process == sampling_process;
}
