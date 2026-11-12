// capture.c: Stage 7, collecting 128-point blocks for the FFT (see capture.h
// for the why and the how-to-use).
//
// Two flags hand the buffer back and forth, so main and the ISR never use it at
// the same time:
//   capture_armed  1 = the ISR owns the buffer and is filling it
//   capture_full   1 = main owns the buffer and can read it
// Main sets armed, the ISR clears armed and sets full, main clears full when it
// asks for the next block.

#include "capture.h"
#include "debug.h"

// 1 while the ISR is filling the buffer. Set by capture_start(), cleared by the
// ISR after the last point (or by capture_stop())
volatile uint32_t capture_armed = 0U;

// Set by the ISR when the block is full, cleared by capture_start()
static volatile uint32_t capture_full = 0U;

// The block. Only the ISR writes it, and only while capture_armed is 1
static uint16_t buffer[CAPTURE_POINTS];

// ISR working state. Main sets these up before arming and never touches them while
// armed, so they don't need to be volatile
static uint32_t fill_index;		// next point to store
static uint32_t sum;			// samples added so far for this point
static uint32_t sum_count;		// how many samples are in sum
static uint32_t decim_log2;		// 0 to 3: 1, 2, 4 or 8 samples per point
static uint32_t decim;			// 1 << decim_log2
static uint32_t first_cycle;	// cycle counter at point 0
static uint32_t last_cycle;		// cycle counter at point 127

// Main loop: set up a fresh block and hand the buffer to the ISR
void capture_start(uint32_t rate_hz) {
	// Stop the ISR first, so it can't see a half-set-up block
	capture_armed = 0U;
	capture_full = 0U;

	// Pick the samples per point that brings this rate closest to 49.9 kHz. Using
	// the measured rate, not the button the user pressed, means the block is right
	// even with a signal generator, or a clock board that hasn't switched yet
	if (rate_hz > CAPTURE_SPLIT_HZ * 4U) {
		decim_log2 = 3U;
	} else if (rate_hz > CAPTURE_SPLIT_HZ * 2U) {
		decim_log2 = 2U;
	} else if (rate_hz > CAPTURE_SPLIT_HZ) {
		decim_log2 = 1U;
	} else {
		decim_log2 = 0U;
	}
	decim = 1U << decim_log2;

	// Empty block, nothing summed yet
	fill_index = 0U;
	sum = 0U;
	sum_count = 0U;

	// The barrier makes sure every write above has happened before the ISR sees
	// the armed flag. Without it the compiler could move them after this write
	__DMB();
	capture_armed = 1U;
}

// Main loop: abandon any block, part-filled or finished. ui.c calls this when the
// spectrum page closes, so the ISR goes back to doing zero capture work
void capture_stop(void) {
	capture_armed = 0U;		// the ISR stops adding points on its next sample
	capture_full = 0U;
}

// Main loop: has the ISR finished the block?
int capture_ready(void) {
	if (capture_full == 0U) {
		return 0;
	}
	// The ISR filled the buffer before setting capture_full. The barrier stops the
	// compiler reading the buffer early, before it has checked the flag
	__DMB();
	return 1;
}

// The finished block. A pointer, not a copy: read it before capture_start()
const uint16_t *capture_data(void) {
	return buffer;
}

// Samples per point used for the last block (1, 2, 4 or 8)
uint32_t capture_decimation(void) {
	return decim;
}

// How long the block took, in CPU cycles. ui.c turns this into the real capture
// rate, so the frequency axis is right even if the clock board drifts
uint32_t capture_span_cycles(void) {
	return last_cycle - first_cycle;
}

// Runs inside the audio ISR on every sample while armed. Adding up 8 samples and
// keeping 1 is a cheap low-pass before throwing samples away: it averages out
// much of what would fold down. It isn't a sharp filter, so the FFT may still show
// some energy folded down from above 25 kHz at the fast rates
//
// Most calls just add and return. Only every decim-th call stores a point, so at
// fx the expensive path runs once per 8 samples
void capture_push(uint32_t code) {
	// Add this sample to the point being built
	sum += code;
	sum_count++;
	if (sum_count < decim) {
		return;
	}

	// Scale to 8 samples' worth, so every rate gives the same numbers. At fx/8 one
	// sample is shifted left 3 (x 8); at fx eight samples are already 8 samples'
	// worth (shift 0). The biggest value is 8 x 4095 = 32760, which fits uint16_t
	buffer[fill_index] = (uint16_t)(sum << (3U - decim_log2));
	sum = 0U;
	sum_count = 0U;

	// Time the block, so main can work out the real capture rate
	if (fill_index == 0U) {
		first_cycle = dbg_cycles();
	}
	fill_index++;

	// Full: stop, and tell main. Buffer writes first, then the flag
	if (fill_index >= CAPTURE_POINTS) {
		last_cycle = dbg_cycles();
		capture_armed = 0U;
		__DMB();
		capture_full = 1U;
	}
}
