#include "audio.h"
#include "adc.h"
#include "dac.h"
#include "debug.h"
#include "extclk.h"
#include "capture.h"

volatile uint32_t audio_overruns = 0U;
volatile uint32_t audio_late_edges = 0U;
volatile uint32_t audio_max_cycles = 0U;

audio_process_t volatile audio_process = 0;

// The sample to send on the next edge. Only the ISR touches this
static uint32_t next_output = DAC_MID_CODE;

// Sets up the ADC and DAC, then the clock last, so the ISR finds them ready
void audio_init(void) {
	adc_init();
	dac_init();

	// Run one conversion now, so the first edge has a result to collect
	adc_start();
	while (!adc_is_done()) {
	}

	extclk_init();
}

// Runs on every clock edge. The name must match the startup file exactly, or the
// default handler runs instead and hangs
void EXTI15_10_IRQHandler(void) {
	uint32_t start = dbg_cycles();
	uint32_t cycles;
	uint32_t code = 0U;
	uint32_t have_sample = 0U;
	audio_process_t process;

	dbg_scope_high();				// PF8 stays high while the ISR runs

	// Writing 1 clears the pending flag. Left set, this ISR would fire forever
	EXTI->PR = EXTCLK_PR_FLAG;
	extclk_edges++;

	// Output first, so the DAC updates at the same time after every edge
	dac_write(next_output);

	// Collect the conversion started on the last edge. We never wait for the ADC:
	// if it isn't done, the clock is too fast, so count it and repeat the last sample
	if (adc_is_done()) {
		code = adc_result();
		have_sample = 1U;
	} else {
		audio_overruns++;
	}

	// Sample the input for the next edge. This is the sampling instant. It comes
	// before any processing, so however long the mixer takes, we always sample at
	// the same time after the edge
	adc_start();

	// Passthrough, or the processing installed in audio_process (Sampling/sampling.c).
	// The result goes to the DAC on the next edge
	if (have_sample) {
		process = audio_process;
		next_output = (process != 0) ? process(code) : code;

		// Stage 7: hand the raw input to the FFT capture, but only while main has
		// asked for a block. Idle, this is one load and one branch
		if (capture_armed != 0U) {
			capture_push(code);
		}
	}

	// A pending flag here means the next edge came before we finished
	if (EXTI->PR & EXTCLK_PR_FLAG) {
		audio_late_edges++;
	}

	dbg_scope_low();

	// Track the longest run. This misses interrupt entry and exit, about 30 cycles
	cycles = dbg_cycles() - start;
	if (cycles > audio_max_cycles) {
		audio_max_cycles = cycles;
	}
}
