#include "capture.h"
#include "debug.h"

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

	fill_index = 0U;
	sum = 0U;
	sum_count = 0U;

	// The barrier makes sure every write above has happened before the ISR sees
	// the armed flag. Without it the compiler could move them after this write
	__DMB();
	capture_armed = 1U;
}

int capture_ready(void) {
	if (capture_full == 0U) {
		return 0;
	}
	// The ISR filled the buffer before setting capture_full. The barrier stops the
	// compiler reading the buffer early, before it has checked the flag
	__DMB();
	return 1;
}

const uint16_t *capture_data(void) {
	return buffer;
}

uint32_t capture_decimation(void) {
	return decim;
}

uint32_t capture_span_cycles(void) {
	return last_cycle - first_cycle;
}

// Runs inside the audio ISR on every sample while armed. Adding up 8 samples and
// keeping 1 is a cheap low-pass before throwing samples away: it averages out
// much of what would fold down. It isn't a sharp filter, so the FFT may still show
// some energy folded down from above 25 kHz at the fast rates
void capture_push(uint32_t code) {
	sum += code;
	sum_count++;
	if (sum_count < decim) {
		return;
	}

	// Scale to 8 samples' worth, so every rate gives the same numbers
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
