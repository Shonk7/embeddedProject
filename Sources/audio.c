// ===========================================================================
// audio.c: the audio interrupt. This is the heart of the core objective
// ===========================================================================
//
// What happens on every clock edge, in order (see EXTI15_10_IRQHandler below):
//   1. Clear the interrupt flag and count the edge
//   2. Write the DAC with the sample we worked out on the previous edge
//   3. Collect the ADC result started on the previous edge
//   4. Start the next ADC conversion: this is the sampling instant
//   5. Process the sample we just collected (passthrough, or the mixer at fx/8),
//      ready for the DAC on the next edge. Also feed the FFT capture if it's on
//   6. Check whether the next edge already arrived while we were busy
//
// Doing the fixed-time jobs (DAC write, ADC start) first and the variable-time job
// (processing) last means the sampling and output instants sit at the same delay
// after every edge, however long the mixer takes. That keeps jitter out of the
// sample timing, which the spec says is the same as adding noise.
//
// The cost is a short pipeline delay: a sample taken on edge n comes out of the
// DAC on edge n + 2. That's 5 us at fx and 40 us at fx/8, far too short to hear.

#include "audio.h"
#include "adc.h"
#include "dac.h"
#include "debug.h"
#include "extclk.h"
#include "capture.h"

// Health counters (see audio.h). Only this file writes them
volatile uint32_t audio_overruns = 0U;
volatile uint32_t audio_late_edges = 0U;
volatile uint32_t audio_max_cycles = 0U;

// The processing hook. 0 = passthrough until sampling_init() installs the mixer path
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

		// Stage 7: hand the raw input (before the mixer, because the spec wants the
		// spectrum of the input) to the FFT capture, but only while the spectrum
		// page has asked for a block. Idle, this is one load and one branch
		if (capture_armed != 0U) {
			capture_push(code);
		}
	}

	// A pending flag here means the next edge came before we finished. The
	// hardware remembers one pending edge, so it isn't lost yet, but if this keeps
	// happening we're over budget and a second edge would be
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
