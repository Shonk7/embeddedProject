#include <math.h>
#include "mixer.h"

// Fixed-point formats. These must match P in mixer_design.m
#define COEF_BITS	30U								// coefficients are Q2.30 (range +-2)
#define GAIN_BITS	14U								// gains are Q14 (1.0 = 16384)
#define OUT_SHIFT	(GAIN_BITS + MIXER_STATE_BITS)	// 22: back to ADC counts
#define ERR_MASK	((1U << COEF_BITS) - 1U)		// the 30 bits that ">> 30" drops

#define OUT_MIN		(-2048)
#define OUT_MAX		2047

#define PI			3.14159265358979323846

// One biquad's coefficients, a0 = 1. The filter does
//   acc = b0 x[n] + b1 x[n-1] + b2 x[n-2] - a1 y[n-1] - a2 y[n-2]
typedef struct {
	int32_t b0, b1, b2, a1, a2;
} coef_t;

// One biquad's memory. err holds the bits dropped last time (error feedback)
typedef struct {
	int32_t x1, x2, y1, y2;
	uint32_t err;
} state_t;

// The four different filters. Each one is used twice in a row (LR4)
#define LP1		0U		// low-pass at fc1
#define HP1		1U		// high-pass at fc1
#define LP2		2U		// low-pass at fc2
#define HP2		3U		// high-pass at fc2
#define NUM_COEF_SETS	4U

// Two banks of coefficients. The ISR reads bank[live_bank]. Main writes the other
// bank, then flips live_bank in one write, so the ISR never sees a half-updated set
static coef_t bank[2][NUM_COEF_SETS];
static volatile uint32_t live_bank = 0U;

// 8 biquads: low 2, mid 4, high 2. Each needs its own memory even when it shares
// coefficients, because the second stage filters the first stage's output
#define NUM_STAGES	8U
static state_t stage[NUM_STAGES];

static volatile int32_t gain[MIXER_BANDS] = {
	(int32_t)MIXER_GAIN_ONE, (int32_t)MIXER_GAIN_ONE, (int32_t)MIXER_GAIN_ONE
};

volatile uint32_t mixer_on = 0U;

// Set by main, cleared by the ISR after it empties the filters' memory. Main can't
// clear the memory itself, because the ISR could run halfway through
static volatile uint32_t reset_request = 1U;

static uint32_t fc1_hz = MIXER_FC1_DEFAULT;
static uint32_t fc2_hz = MIXER_FC2_DEFAULT;
static uint32_t fs_used_hz = 0U;

// ---------------------------------------------------------------------------
// Coefficient design (main loop, double maths, only when a cutoff changes)
// ---------------------------------------------------------------------------

// Rounds to the nearest Q2.30 integer, halves away from zero like MATLAB round()
static int32_t to_q30(double v) {
	double scaled = v * (double)(1UL << COEF_BITS);

	return (scaled >= 0.0) ? (int32_t)(scaled + 0.5) : -(int32_t)(-scaled + 0.5);
}

// Butterworth (Q = 1/sqrt(2)) low-pass and high-pass at fc, from the RBJ Audio
// EQ Cookbook. Same formulas as rbjLowpass/rbjHighpass in mixer_design.m. The
// low-pass and high-pass at one frequency share a1 and a2 (same poles)
static void design_pair(coef_t *lp, coef_t *hp, double fc, double fs) {
	double q = 1.0 / sqrt(2.0);
	double w = 2.0 * PI * fc / fs;
	double cw = cos(w);
	double alpha = sin(w) / (2.0 * q);
	double a0 = 1.0 + alpha;

	lp->b0 = to_q30(((1.0 - cw) / 2.0) / a0);
	lp->b1 = to_q30((1.0 - cw) / a0);
	lp->b2 = lp->b0;

	hp->b0 = to_q30(((1.0 + cw) / 2.0) / a0);
	hp->b1 = to_q30(-(1.0 + cw) / a0);
	hp->b2 = hp->b0;

	lp->a1 = to_q30((-2.0 * cw) / a0);
	lp->a2 = to_q30((1.0 - alpha) / a0);
	hp->a1 = lp->a1;
	hp->a2 = lp->a2;
}

int mixer_set_crossovers(uint32_t fc1, uint32_t fc2, uint32_t fs) {
	uint32_t spare = live_bank ^ 1U;

	if ((fc1 < MIXER_FC1_MIN) || (fc1 > MIXER_FC1_MAX) ||
	    (fc2 < MIXER_FC2_MIN) || (fc2 > MIXER_FC2_MAX) ||
	    (fs < 4U * fc2)) {
		return 0;
	}

	// Fill the bank the ISR isn't using, then switch to it in one write
	design_pair(&bank[spare][LP1], &bank[spare][HP1], (double)fc1, (double)fs);
	design_pair(&bank[spare][LP2], &bank[spare][HP2], (double)fc2, (double)fs);
	live_bank = spare;

	fc1_hz = fc1;
	fc2_hz = fc2;
	fs_used_hz = fs;
	return 1;
}

void mixer_init(uint32_t fs_hz) {
	uint32_t b;

	mixer_on = 0U;
	for (b = 0U; b < MIXER_BANDS; b++) {
		gain[b] = (int32_t)MIXER_GAIN_ONE;
	}
	(void)mixer_set_crossovers(MIXER_FC1_DEFAULT, MIXER_FC2_DEFAULT, fs_hz);
	reset_request = 1U;
}

void mixer_set_gain(uint32_t band, uint32_t gain_q14) {
	if (band >= MIXER_BANDS) {
		return;
	}
	if (gain_q14 > MIXER_GAIN_MAX) {
		gain_q14 = MIXER_GAIN_MAX;
	}
	gain[band] = (int32_t)gain_q14;		// one aligned 32-bit write: the ISR sees old or new
}

uint32_t mixer_get_gain(uint32_t band) {
	return (band < MIXER_BANDS) ? (uint32_t)gain[band] : 0U;
}

uint32_t mixer_fc1(void) {
	return fc1_hz;
}

uint32_t mixer_fc2(void) {
	return fc2_hz;
}

uint32_t mixer_fs(void) {
	return fs_used_hz;
}

void mixer_set_active(int on) {
	if (on && (mixer_on == 0U)) {
		reset_request = 1U;		// start from silence, not from old memory
	}
	mixer_on = on ? 1U : 0U;
}

// ---------------------------------------------------------------------------
// Per-sample processing (ISR)
// ---------------------------------------------------------------------------

// One Direct Form I biquad with first-order error feedback, as biquadFixed() in
// mixer_design.m. The products are 32 x 32 -> 64 bit (SMULL/SMLAL on the M3).
// err carries the 30 bits that ">> 30" throws away into the next sample, which
// stops the 500 Hz low-pass sticking at a small DC level (a deadband limit cycle)
static inline int32_t biquad(const coef_t *c, state_t *s, int32_t x) {
	int64_t acc = (int64_t)c->b0 * x
	            + (int64_t)c->b1 * s->x1
	            + (int64_t)c->b2 * s->x2
	            - (int64_t)c->a1 * s->y1
	            - (int64_t)c->a2 * s->y2
	            + (int64_t)s->err;
	int32_t y = (int32_t)(acc >> COEF_BITS);	// arithmetic shift = floor

	s->err = (uint32_t)acc & ERR_MASK;			// = acc - (y << 30)
	s->x2 = s->x1;
	s->x1 = x;
	s->y2 = s->y1;
	s->y1 = y;
	return y;
}

int32_t mixer_process(int32_t x) {
	const coef_t *c;
	int32_t low, mid, high, y;
	int64_t sum;
	uint32_t i;

	if (reset_request != 0U) {
		for (i = 0U; i < NUM_STAGES; i++) {
			stage[i].x1 = 0;
			stage[i].x2 = 0;
			stage[i].y1 = 0;
			stage[i].y2 = 0;
			stage[i].err = 0U;
		}
		reset_request = 0U;
	}

	c = bank[live_bank];

	// Split into three bands (each chain starts from the same input)
	low  = biquad(&c[LP1], &stage[0], x);
	low  = biquad(&c[LP1], &stage[1], low);

	mid  = biquad(&c[HP1], &stage[2], x);
	mid  = biquad(&c[HP1], &stage[3], mid);
	mid  = biquad(&c[LP2], &stage[4], mid);
	mid  = biquad(&c[LP2], &stage[5], mid);

	high = biquad(&c[HP2], &stage[6], x);
	high = biquad(&c[HP2], &stage[7], high);

	// Apply the slider gains and add the bands
	sum = (int64_t)gain[MIXER_LOW] * low
	    + (int64_t)gain[MIXER_MID] * mid
	    + (int64_t)gain[MIXER_HIGH] * high;

	// Back to ADC counts, rounded: remove the 14 gain bits and the 8 state bits
	y = (int32_t)((sum + (1LL << (OUT_SHIFT - 1U))) >> OUT_SHIFT);

	// Gains up to 2 can push past the 12-bit range, so clip instead of wrapping
	if (y > OUT_MAX) {
		y = OUT_MAX;
	} else if (y < OUT_MIN) {
		y = OUT_MIN;
	}
	return y;
}
