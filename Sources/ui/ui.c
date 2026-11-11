#include "../../Headers/stm32f103xe.h"
#include "../../LCD/lcd_grph.h"
#include "../../LCD/font5x7.h"
#include "../clock.h"
#include "../debug.h"
#include "../audio.h"
#include "../extclk.h"
#include "../Sampling/sampling.h"
#include "rate_ctrl.h"
#include "mixer.h"
#include "ui.h"
#include "touch.h"

// ---------------------------------------------------------------------------
// Layout (portrait 240 x 320). Every touch area is a rectangle
// ---------------------------------------------------------------------------
typedef struct {
	uint16_t x0, y0, x1, y1;
} box_t;

static const box_t btn_rate[RATE_COUNT] = {
	{  8, 116, 115, 161 },	// fx    top left
	{ 124, 116, 231, 161 },	// fx/2  top right
	{  8, 168, 115, 213 },	// fx/4  bottom left
	{ 124, 168, 231, 213 }	// fx/8  bottom right
};
static const box_t btn_enable   = {   8, 224, 115, 263 };	// left half
static const box_t btn_invalid  = { 124, 224, 231, 263 };	// right half: sends 0xD3
static const box_t btn_mixer    = {   8, 272, 115, 303 };
static const box_t btn_spectrum = { 124, 272, 231, 303 };
static const box_t btn_back     = {   8, 272, 231, 303 };

#define STATUS_X0	8U
#define STATUS_Y0	40U
#define STATUS_X1	231U
#define STATUS_Y1	95U

typedef enum {
	PAGE_MAIN = 0,
	PAGE_MIXER,
	PAGE_SPECTRUM,
	PAGE_STATS
} page_t;

static page_t page = PAGE_MAIN;

volatile uint16_t ui_test_x = 0xFFFFU;
volatile uint16_t ui_test_y = 0U;

// ---------------------------------------------------------------------------
// Drawing helpers
// ---------------------------------------------------------------------------
static uint16_t text_len(const char *s) {
	uint16_t n = 0U;
	while (s[n] != '\0') {
		n++;
	}
	return n;
}

// The library font is 6 x 8 pixels. This draws it at double size (12 x 16) by
// filling a 2 x 2 block per font pixel, so buttons are readable and touchable
static void text2x(uint16_t x, uint16_t y, const char *s, lcd_color_t fg, lcd_color_t bg) {
	uint16_t row, col;
	unsigned char ch, bits;

	while (*s != '\0') {
		ch = (unsigned char)*s++;
		if ((ch < 0x20U) || (ch > 0x7FU)) {
			ch = 0x20U;
		}
		for (row = 0U; row < 8U; row++) {
			bits = font5x7[ch - 0x20U][row];
			for (col = 0U; col < 6U; col++) {
				lcd_fillRect(x + 2U * col, y + 2U * row,
				             x + 2U * col + 1U, y + 2U * row + 1U,
				             (bits & (0x80U >> col)) ? fg : bg);
			}
		}
		x += 12U;
	}
}

static void text1x(uint16_t x, uint16_t y, const char *s, lcd_color_t fg, lcd_color_t bg) {
	lcd_fontColor(fg, bg);
	lcd_putString(x, y, (unsigned char *)s);
}

static void draw_button(const box_t *b, const char *label, lcd_color_t fg, lcd_color_t bg) {
	uint16_t w = b->x1 - b->x0 + 1U;
	uint16_t h = b->y1 - b->y0 + 1U;
	uint16_t tw = text_len(label) * 12U;

	lcd_fillRect(b->x0, b->y0, b->x1, b->y1, bg);
	lcd_drawRect(b->x0, b->y0, b->x1, b->y1, LIGHT_GRAY);
	text2x(b->x0 + (w - tw) / 2U, b->y0 + (h - 16U) / 2U, label, fg, bg);
}

static void draw_title(const char *title) {
	uint16_t tw = text_len(title) * 12U;

	lcd_fillRect(0, 0, 239, 31, NAVY);
	text2x((240U - tw) / 2U, 8U, title, WHITE, NAVY);
}

static int hit(const box_t *b, uint16_t x, uint16_t y) {
	return (x >= b->x0) && (x <= b->x1) && (y >= b->y0) && (y <= b->y1);
}

// ---------------------------------------------------------------------------
// Main page
// ---------------------------------------------------------------------------
static void draw_status(void) {
	char cmd_text[] = "LAST CMD 0x00";

	// Clear the value line, because "99.81 kHz" is shorter than "399.25 kHz"
	lcd_fillRect(STATUS_X0 + 1U, 58U, STATUS_X1 - 1U, 73U, BLACK);
	text2x(16U, 58U, rate_label(rate_get()), WHITE, BLACK);

	// Same length either way, so one overwrites the other completely
	if (rate_is_enabled()) {
		text1x(16U, 82U, "CLOCK: RUNNING", GREEN, BLACK);
	} else {
		text1x(16U, 82U, "CLOCK: STOPPED", RED, BLACK);
	}

	// Last byte sent to the clock board, e.g. "LAST CMD 0xD3". After the invalid
	// command this changes while the rate above stays the same
	cmd_text[11] = "0123456789ABCDEF"[clock_last_cmd >> 4];
	cmd_text[12] = "0123456789ABCDEF"[clock_last_cmd & 0xFU];
	text1x(136U, 82U, cmd_text, LIGHT_GRAY, BLACK);
}

// Selected rate: green while running, yellow while stopped (the board holds
// the rate across a disable, spec p.3 req 5)
static void draw_rate_buttons(void) {
	rate_t r;

	for (r = RATE_FX; r < RATE_COUNT; r++) {
		if (r == rate_get()) {
			draw_button(&btn_rate[r], rate_short_label(r), RED,
			            rate_is_enabled() ? GREEN : YELLOW);
		} else {
			draw_button(&btn_rate[r], rate_short_label(r), WHITE, NAVY);
		}
	}
}

// Shows the action the button will do: CLKOFF while running, CLKON while stopped
static void draw_enable_button(void) {
	if (rate_is_enabled()) {
		draw_button(&btn_enable, "CLKOFF", WHITE, RED);
	} else {
		draw_button(&btn_enable, "CLKON", BLACK, GREEN);
	}
}

// Test button for the invalid-command requirement
static void draw_invalid_button(void) {
	draw_button(&btn_invalid, "BAD RATE", WHITE, MAROON);
}

static void draw_main_page(void) {
	//lcd_fillScreen(BLACK);
	draw_title("AUDIO SAMPLER");

	lcd_drawRect(STATUS_X0, STATUS_Y0, STATUS_X1, STATUS_Y1, LIGHT_GRAY);
	text1x(16U, 46U, "SAMPLE RATE", LIGHT_GRAY, BLACK);
	text1x(150U, 46U, "TAP FOR STATS", DARK_GRAY, BLACK);
	draw_status();

	text1x(8U, 104U, "SELECT RATE", LIGHT_GRAY, BLACK);
	draw_rate_buttons();
	draw_enable_button();
	draw_invalid_button();

	draw_button(&btn_mixer, "MIXER", WHITE, DARK_CYAN);
	draw_button(&btn_spectrum, "SPECTRUM", WHITE, DARK_CYAN);

	// Backup controls (touch also works)
	text1x(8U, 308U, "PB0: next rate  PB1: start/stop", DARK_GRAY, BLACK);
}

// ---------------------------------------------------------------------------
// Placeholder page (spectrum, until Ext B exists)
// ---------------------------------------------------------------------------
static void draw_placeholder_page(const char *title, const char *line) {
	lcd_fillScreen(BLACK);
	draw_title(title);
	text1x(16U, 60U, line, WHITE, BLACK);
	text1x(16U, 76U, "Page not built yet", LIGHT_GRAY, BLACK);
	text1x(16U, 100U, "Rate:", LIGHT_GRAY, BLACK);
	text1x(52U, 100U, rate_label(rate_get()), WHITE, BLACK);
	draw_button(&btn_back, "BACK", WHITE, DARK_CYAN);
}

static void show_page(page_t p);

// ---------------------------------------------------------------------------
// Mixer page (Ext A). Three gain bars: tap a bar to set its gain, 0 to 2 in
// steps of 0.05 (the white mark is 1.0). The crossovers are fixed for the whole
// project at 500 Hz and 4 kHz and are only displayed. Two lines of live stats:
// ISR load, measured rate, late edges, clips
// ---------------------------------------------------------------------------
#define MIX_STATUS_Y	36U
#define MIX_ROW_Y0		50U		// top of the first gain row
#define MIX_ROW_PITCH	44U		// distance between rows
#define MIX_BAR_X0		8U
#define MIX_BAR_X1		231U
#define MIX_BAR_TOP		18U		// bar position inside its row
#define MIX_BAR_BOT		37U
#define MIX_BAR_W		(MIX_BAR_X1 - MIX_BAR_X0 - 1U)	// 222 pixels inside the frame
#define MIX_VALUE_X		184U	// "1.00" in big text, right of the label
#define MIX_FC_LABEL_Y	186U
#define MIX_STATS_Y		254U

// Gains are kept here in hundredths (100 = 1.00) so the display is exact
#define GAIN_MAX_H		200U
#define GAIN_STEP_H		5U		// 0.05 per step

// Interrupt entry and exit, which the ISR's own cycle count can't see (Stage 5)
#define ISR_ENTRY_EXIT_CYCLES	30U

static const box_t btn_mix_back = {   8, 272, 115, 303 };
static const box_t btn_mix_flat = { 124, 272, 231, 303 };

static const lcd_color_t band_colour[MIXER_BANDS] = { MAGENTA, GREEN, CYAN };

static uint32_t gain_h[MIXER_BANDS] = { 100U, 100U, 100U };

// Measured every half second by stats_update()
static uint32_t stat_last_tick;
static uint32_t stat_last_edges;
static uint32_t stat_rate_hz;
static uint32_t stat_isr_cycles;

// Writes v in decimal at p and returns the new end of the string
static char *put_uint(char *p, uint32_t v) {
	char digits[10];
	uint32_t n = 0U;

	do {
		digits[n++] = (char)('0' + (v % 10U));
		v /= 10U;
	} while (v != 0U);
	while (n > 0U) {
		*p++ = digits[--n];
	}
	*p = '\0';
	return p;
}

static char *put_text(char *p, const char *s) {
	while (*s != '\0') {
		*p++ = *s++;
	}
	*p = '\0';
	return p;
}

// Hundredths -> "1.25"
static void gain_text(char *p, uint32_t h) {
	p[0] = (char)('0' + (h / 100U));
	p[1] = '.';
	p[2] = (char)('0' + ((h / 10U) % 10U));
	p[3] = (char)('0' + (h % 10U));
	p[4] = '\0';
}

// Hundredths -> the mixer's Q14 gain (1.00 -> 16384), rounded
static uint32_t gain_q14(uint32_t h) {
	return (h * MIXER_GAIN_ONE + 50U) / 100U;
}

// "LOW   0-500 Hz", "MID   500-4000 Hz", "HIGH  4000+ Hz"
static void band_label(char *p, uint32_t band) {
	if (band == MIXER_LOW) {
		p = put_text(p, "LOW   0-");
		p = put_uint(p, mixer_fc1());
	} else if (band == MIXER_MID) {
		p = put_text(p, "MID   ");
		p = put_uint(p, mixer_fc1());
		p = put_text(p, "-");
		p = put_uint(p, mixer_fc2());
	} else {
		p = put_text(p, "HIGH  ");
		p = put_uint(p, mixer_fc2());
		p = put_text(p, "+");
	}
	(void)put_text(p, " Hz");
}

static void draw_gain_row(uint32_t band) {
	uint16_t y0 = (uint16_t)(MIX_ROW_Y0 + band * MIX_ROW_PITCH);
	uint16_t top = y0 + MIX_BAR_TOP;
	uint16_t bot = y0 + MIX_BAR_BOT;
	uint16_t fill = (uint16_t)((gain_h[band] * MIX_BAR_W) / GAIN_MAX_H);
	uint16_t one_x = MIX_BAR_X0 + 1U + MIX_BAR_W / 2U;
	char text[24];

	// Label and value
	lcd_fillRect(MIX_BAR_X0, y0, MIX_VALUE_X - 1U, y0 + 15U, BLACK);
	band_label(text, band);
	text1x(MIX_BAR_X0, y0 + 4U, text, LIGHT_GRAY, BLACK);
	gain_text(text, gain_h[band]);
	text2x(MIX_VALUE_X, y0, text, WHITE, BLACK);

	// Bar: coloured up to the gain, black after it, white mark at 1.0
	lcd_drawRect(MIX_BAR_X0, top, MIX_BAR_X1, bot, LIGHT_GRAY);
	if (fill > 0U) {
		lcd_fillRect(MIX_BAR_X0 + 1U, top + 1U, MIX_BAR_X0 + fill, bot - 1U, band_colour[band]);
	}
	if (fill < MIX_BAR_W) {
		lcd_fillRect(MIX_BAR_X0 + 1U + fill, top + 1U, MIX_BAR_X1 - 1U, bot - 1U, BLACK);
	}
	lcd_line(one_x, top, one_x, bot, WHITE);
}

static void draw_mixer_status(void) {
	char text[41];
	char *p;

	lcd_fillRect(0U, MIX_STATUS_Y, 239U, MIX_STATUS_Y + 9U, BLACK);
	if (!sampling_is_running()) {
		text1x(8U, MIX_STATUS_Y, "OFF: build with ACTIVE_TEST 10", RED, BLACK);
	} else if (!mixer_is_active()) {
		text1x(8U, MIX_STATUS_Y, "BYPASSED: mixer runs at 50 kHz only", YELLOW, BLACK);
	} else {
		p = put_text(text, "ACTIVE, designed for fs = 50kHz ");
		//p = put_uint(p, mixer_fs());
		//(void)put_text(p, " Hz");
		text1x(8U, MIX_STATUS_Y, text, GREEN, BLACK);
	}
}

// Fixed crossovers, shown for reference: "500 Hz / 4000 Hz"
static void draw_crossovers(void) {
	char text[24];
	char *p;

	text1x(8U, MIX_FC_LABEL_Y, "CROSSOVERS (FIXED)", LIGHT_GRAY, BLACK);
	p = put_uint(text, mixer_fc1());
	p = put_text(p, " Hz / ");
	p = put_uint(p, mixer_fc2());
	(void)put_text(p, " Hz");
	text2x(8U, MIX_FC_LABEL_Y + 16U, text, WHITE, BLACK);
}

// "ISR 412/1443 cycles = 28%" and "fs 49906 Hz  LATE 0  CLIP 0"
static void draw_mixer_stats(void) {
	char text[48];
	char *p;
	uint32_t budget;
	uint32_t used;

	lcd_fillRect(0U, MIX_STATS_Y, 239U, MIX_STATS_Y + 17U, BLACK);
	if (stat_rate_hz == 0U) {
		text1x(8U, MIX_STATS_Y, "NO CLOCK", LIGHT_GRAY, BLACK);
		return;
	}

	budget = SYSCLK_HZ / stat_rate_hz;
	used = stat_isr_cycles + ISR_ENTRY_EXIT_CYCLES;
	p = put_text(text, "ISR ");
	p = put_uint(p, used);
	p = put_text(p, "/");
	p = put_uint(p, budget);
	p = put_text(p, " cycles = ");
	p = put_uint(p, (used * 100U) / budget);
	(void)put_text(p, "%");
	text1x(8U, MIX_STATS_Y, text, WHITE, BLACK);

	p = put_text(text, "fs ");
	p = put_uint(p, stat_rate_hz);
	p = put_text(p, " Hz LATE ");
	p = put_uint(p, audio_late_edges);
	p = put_text(p, " OVR ");
	p = put_uint(p, audio_overruns);
	p = put_text(p, " CLIP ");
	(void)put_uint(p, sampling_clips);
	text1x(8U, MIX_STATS_Y + 10U, text, WHITE, BLACK);
}

static void draw_mixer_page(void) {
	uint32_t b;

	lcd_fillScreen(BLACK);
	draw_title("3-BAND MIXER");
	draw_mixer_status();
	for (b = 0U; b < MIXER_BANDS; b++) {
		draw_gain_row(b);
	}
	draw_crossovers();
	draw_mixer_stats();
	draw_button(&btn_mix_back, "BACK", WHITE, DARK_CYAN);
	draw_button(&btn_mix_flat, "FLAT", WHITE, DARK_CYAN);
}

// Tap position on a bar -> gain, snapped to 0.05
static void set_gain_from_x(uint32_t band, uint16_t x) {
	uint32_t pos = (x > MIX_BAR_X0 + 1U) ? (uint32_t)(x - (MIX_BAR_X0 + 1U)) : 0U;
	uint32_t h;

	if (pos > MIX_BAR_W) {
		pos = MIX_BAR_W;
	}
	h = (pos * GAIN_MAX_H + MIX_BAR_W / 2U) / MIX_BAR_W;
	h = ((h + GAIN_STEP_H / 2U) / GAIN_STEP_H) * GAIN_STEP_H;

	gain_h[band] = h;
	mixer_set_gain(band, gain_q14(h));
	draw_gain_row(band);
}

static void mixer_touch(uint16_t x, uint16_t y) {
	uint32_t b;
	uint16_t y0;

	if (hit(&btn_mix_back, x, y)) {
		show_page(PAGE_MAIN);
		return;
	}
	if (hit(&btn_mix_flat, x, y)) {
		for (b = 0U; b < MIXER_BANDS; b++) {
			gain_h[b] = 100U;
			mixer_set_gain(b, MIXER_GAIN_ONE);
			draw_gain_row(b);
		}
		return;
	}

	// Gain bars, with a few pixels of slack above and below each bar
	for (b = 0U; b < MIXER_BANDS; b++) {
		y0 = (uint16_t)(MIX_ROW_Y0 + b * MIX_ROW_PITCH);
		if ((y + 6U >= y0 + MIX_BAR_TOP) && (y <= y0 + MIX_BAR_BOT + 3U) &&
		    (x >= MIX_BAR_X0) && (x <= MIX_BAR_X1)) {
			set_gain_from_x(b, x);
			return;
		}
	}
}

// ---------------------------------------------------------------------------
// Stats page: the no-dropped-samples evidence, live, in the full system. Open it
// by tapping the SAMPLE RATE box on the main page. Counts are shown since the
// last RESET. Main keeps baselines and subtracts them, so it never writes the
// ISR's own counters (no race with the ISR)
// ---------------------------------------------------------------------------
#define ST_ROW_Y0		40U
#define ST_ROW_PITCH	22U
#define ST_VALUE_X		112U	// 10 big digits fit: 112 + 10 x 12 = 232
#define ST_MIXER_Y		224U
#define ST_VERDICT_Y	244U

static const box_t btn_st_reset = {   8, 272, 115, 303 };
static const box_t btn_st_back  = { 124, 272, 231, 303 };

// Values at the last RESET
static uint32_t base_edges;
static uint32_t base_overruns;
static uint32_t base_late;
static uint32_t base_clips;
static uint32_t stat_isr_peak;		// longest ISR since RESET, without entry/exit

static const char * const st_labels[] = {
	"RATE (Hz)", "EDGES", "OVERRUNS", "LATE EDGES",
	"ISR PEAK (cyc)", "BUDGET (cyc)", "ISR LOAD (%)", "CLIPS"
};
#define ST_ROWS		(sizeof(st_labels) / sizeof(st_labels[0]))
#define ST_OVERRUNS	2U
#define ST_LATE		3U

static void stats_reset(void) {
	base_edges = extclk_edges;
	base_overruns = audio_overruns;
	base_late = audio_late_edges;
	base_clips = sampling_clips;
	stat_isr_peak = 0U;
}

// The numbers and the verdict line. Called every half second while shown
static void draw_stats_values(void) {
	uint32_t v[ST_ROWS];
	uint32_t overruns = audio_overruns - base_overruns;
	uint32_t late = audio_late_edges - base_late;
	uint32_t budget = (stat_rate_hz != 0U) ? (SYSCLK_HZ / stat_rate_hz) : 0U;
	uint32_t used = stat_isr_peak + ISR_ENTRY_EXIT_CYCLES;
	uint32_t i;
	uint16_t y;
	char text[12];

	v[0] = stat_rate_hz;
	v[1] = extclk_edges - base_edges;	// burst test: must rise by exactly N
	v[ST_OVERRUNS] = overruns;			// ADC not done: a repeated sample
	v[ST_LATE] = late;					// edge during the ISR: risk of a lost sample
	v[4] = used;
	v[5] = budget;
	v[6] = (budget != 0U) ? ((used * 100U) / budget) : 0U;
	v[7] = sampling_clips - base_clips;

	for (i = 0U; i < ST_ROWS; i++) {
		y = (uint16_t)(ST_ROW_Y0 + i * ST_ROW_PITCH);
		(void)put_uint(text, v[i]);
		lcd_fillRect(ST_VALUE_X, y, 231U, y + 15U, BLACK);
		text2x(ST_VALUE_X, y, text,
		       (((i == ST_OVERRUNS) || (i == ST_LATE)) && (v[i] != 0U)) ? RED : WHITE, BLACK);
	}

	lcd_fillRect(0U, ST_VERDICT_Y, 239U, ST_VERDICT_Y + 15U, BLACK);
	if (stat_rate_hz == 0U) {
		text2x(8U, ST_VERDICT_Y, "NO CLOCK", YELLOW, BLACK);
	} else if ((overruns == 0U) && (late == 0U)) {
		text2x(8U, ST_VERDICT_Y, "NO DROPPED SAMPLES", GREEN, BLACK);
	} else {
		text2x(8U, ST_VERDICT_Y, "SAMPLES DROPPED", RED, BLACK);
	}
}

static void draw_stats_page(void) {
	uint32_t i;

	lcd_fillScreen(BLACK);
	draw_title("STATS");
	for (i = 0U; i < ST_ROWS; i++) {
		text1x(8U, (uint16_t)(ST_ROW_Y0 + i * ST_ROW_PITCH + 4U), st_labels[i], LIGHT_GRAY, BLACK);
	}

	// Which path the samples take right now
	text1x(8U, ST_MIXER_Y, "AUDIO:", LIGHT_GRAY, BLACK);
	if (!sampling_is_running()) {
		text1x(50U, ST_MIXER_Y, "PASSTHROUGH (no Sampling)", WHITE, BLACK);
	} else if (mixer_is_active()) {
		text1x(50U, ST_MIXER_Y, "SAMPLING + MIXER (fx/8)", GREEN, BLACK);
	} else {
		text1x(50U, ST_MIXER_Y, "SAMPLING, MIXER BYPASSED", YELLOW, BLACK);
	}

	draw_stats_values();
	draw_button(&btn_st_reset, "RESET", WHITE, MAROON);
	draw_button(&btn_st_back, "BACK", WHITE, DARK_CYAN);
}

static void stats_touch(uint16_t x, uint16_t y) {
	if (hit(&btn_st_back, x, y)) {
		show_page(PAGE_MAIN);
	} else if (hit(&btn_st_reset, x, y)) {
		stats_reset();
		draw_stats_values();
	}
}

// Every half second: measure the real sample rate and the longest ISR. If the
// mixer is on and the measured rate is more than 1 % from the rate its filters
// were designed for, redesign them, so the crossovers land where the UI says.
// Our clock is an RC oscillator, so the real rate isn't exactly 49906 Hz
#define STATS_PERIOD_CYCLES	(SYSCLK_HZ / 2U)

static void stats_update(void) {
	uint32_t now = dbg_cycles();
	uint32_t elapsed = now - stat_last_tick;
	uint32_t edges;
	uint32_t fs_nominal = rate_hz(RATE_FX_8);
	uint32_t fs_design;

	if (elapsed < STATS_PERIOD_CYCLES) {
		return;
	}
	edges = extclk_edges;
	stat_rate_hz = (uint32_t)((((uint64_t)(edges - stat_last_edges) * SYSCLK_HZ) + elapsed / 2U) / elapsed);
	stat_last_edges = edges;
	stat_last_tick = now;

	stat_isr_cycles = audio_max_cycles;
	audio_max_cycles = 0U;
	if (stat_isr_cycles > stat_isr_peak) {
		stat_isr_peak = stat_isr_cycles;
	}

	fs_design = mixer_fs();
	if (mixer_is_active() &&
	    (stat_rate_hz > fs_nominal / 2U) && (stat_rate_hz < fs_nominal + fs_nominal / 2U) &&
	    ((stat_rate_hz > fs_design + fs_design / 100U) || (stat_rate_hz < fs_design - fs_design / 100U))) {
		(void)mixer_set_crossovers(mixer_fc1(), mixer_fc2(), stat_rate_hz);
		if (page == PAGE_MIXER) {
			draw_mixer_status();
		}
	}

	if (page == PAGE_MIXER) {
		draw_mixer_stats();
	} else if (page == PAGE_STATS) {
		draw_stats_values();
	}
}

static void show_page(page_t p) {
	page_t from = page;

	page = p;
	switch (p) {
	case PAGE_MIXER:
		draw_mixer_page();
		break;
	case PAGE_SPECTRUM:
		draw_placeholder_page("SPECTRUM", "Ext B: FFT spectrum");
		break;
	case PAGE_STATS:
		draw_stats_page();
		break;
	default:
		// Clear what the other page drew. At start-up main.c has just cleared it
		if (from != PAGE_MAIN) {
			lcd_fillScreen(BLACK);
		}
		draw_main_page();
		break;
	}
}

// ---------------------------------------------------------------------------
// Actions
// ---------------------------------------------------------------------------
static void select_rate(rate_t r) {
	if (r == rate_get()) {
		return;		// already selected: no command, no redraw
	}

	// The mixer only fits the cycle budget at fx/8. Switch it off before a faster
	// rate starts, and on only after the slow rate is running (the redraw below
	// takes milliseconds, far longer than the clock board needs to switch)
	if (r != RATE_FX_8) {
		mixer_set_active(0);
	}
	rate_set(r);
	draw_status();
	draw_rate_buttons();
	mixer_set_active(r == RATE_FX_8);
}

static void toggle_enable(void) {
	rate_enable(!rate_is_enabled());
	draw_status();
	draw_rate_buttons();
	draw_enable_button();
}

// Sends the invalid command, then redraws only the status box. The rate, clock
// state and LEDs should all stay as they were; only LAST CMD changes to 0xD3
static void send_invalid(void) {
	rate_send_invalid();
	draw_status();
}

void ui_touch(uint16_t x, uint16_t y) {
	rate_t r;

	if (page == PAGE_MIXER) {
		mixer_touch(x, y);
		return;
	}
	if (page == PAGE_STATS) {
		stats_touch(x, y);
		return;
	}
	if (page != PAGE_MAIN) {
		if (hit(&btn_back, x, y)) {
			show_page(PAGE_MAIN);
		}
		return;
	}

	for (r = RATE_FX; r < RATE_COUNT; r++) {
		if (hit(&btn_rate[r], x, y)) {
			select_rate(r);
			return;
		}
	}
	if (hit(&btn_enable, x, y)) {
		toggle_enable();
	} else if (hit(&btn_invalid, x, y)) {
		send_invalid();
	} else if (hit(&btn_mixer, x, y)) {
		show_page(PAGE_MIXER);
	} else if (hit(&btn_spectrum, x, y)) {
		show_page(PAGE_SPECTRUM);
	} else if ((x >= STATUS_X0) && (x <= STATUS_X1) && (y >= STATUS_Y0) && (y <= STATUS_Y1)) {
		show_page(PAGE_STATS);			// tap the SAMPLE RATE box
	}
}

// ---------------------------------------------------------------------------
// Temporary controls: push buttons PB0 = PE0, PB1 = PE1.
// PE0/PE1 stay in their reset state (floating input, the board drives them),
// so no GPIOE configuration is written - the LCD owns port E.
// Their idle level is recorded at start-up, so it doesn't matter whether the
// buttons pull high or low when pressed
// ---------------------------------------------------------------------------
#define BTN_MASK			0x3U
#define BTN_POLL_CYCLES		(SYSCLK_HZ / 100U)	// sample every 10 ms
#define BTN_STABLE_POLLS	3U					// 30 ms debounce

static uint32_t btn_idle;
static uint32_t btn_state;		// debounced: bit set = pressed
static uint32_t btn_last_raw;
static uint32_t btn_count;
static uint32_t btn_last_poll;

static void buttons_init(void) {
	btn_idle = GPIOE->IDR & BTN_MASK;
	btn_state = 0U;
	btn_last_raw = 0U;
	btn_count = 0U;
	btn_last_poll = dbg_cycles();
}

// Returns the buttons that have just been pressed (bit 0 = PB0, bit 1 = PB1)
static uint32_t buttons_pressed(void) {
	uint32_t raw, newly;

	if ((dbg_cycles() - btn_last_poll) < BTN_POLL_CYCLES) {
		return 0U;
	}
	btn_last_poll = dbg_cycles();

	raw = (GPIOE->IDR ^ btn_idle) & BTN_MASK;
	if (raw != btn_last_raw) {
		btn_last_raw = raw;
		btn_count = 0U;
		return 0U;
	}
	if (btn_count < BTN_STABLE_POLLS) {
		btn_count++;
		if (btn_count == BTN_STABLE_POLLS) {
			newly = raw & ~btn_state;
			btn_state = raw;
			return newly;
		}
	}
	return 0U;
}

// ---------------------------------------------------------------------------
void ui_init(void) {
	// Mixer: filters designed for the nominal fx/8 rate until stats_update()
	// measures the real one. On only if the clock board is at fx/8
	mixer_init(rate_hz(RATE_FX_8));
	mixer_set_active(rate_get() == RATE_FX_8);
	stat_last_tick = dbg_cycles();
	stat_last_edges = extclk_edges;
	stats_reset();		// stats page counts from start-up until the first RESET

	touch_init();
	buttons_init();
	show_page(PAGE_MAIN);
}

void ui_poll(void) {
	uint32_t pressed = buttons_pressed();
	uint16_t tx, ty;

	if (touch_poll(&tx, &ty)) {
		ui_touch(tx, ty);
	}

	stats_update();

	if (pressed != 0U) {
		if (page != PAGE_MAIN) {
			show_page(PAGE_MAIN);				// either button = back
		} else if (pressed & 0x1U) {
			select_rate((rate_t)(((uint32_t)rate_get() + 1U) % RATE_COUNT));
		} else {
			toggle_enable();
		}
	}

	if (ui_test_x != 0xFFFFU) {
		ui_touch(ui_test_x, ui_test_y);
		ui_test_x = 0xFFFFU;
	}
}
