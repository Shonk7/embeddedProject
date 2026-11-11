#include "../../Headers/stm32f103xe.h"
#include "../../LCD/lcd_grph.h"
#include "../../LCD/font5x7.h"
#include "../clock.h"
#include "../debug.h"
#include "../audio.h"
#include "../extclk.h"
#include "../Sampling/sampling.h"
#include "../capture.h"
#include "rate_ctrl.h"
#include "mixer.h"
#include "fft.h"
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
	PAGE_SPECTRUM
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
	draw_button(&btn_invalid, "D3 BAD", WHITE, MAROON);
}

static void draw_main_page(void) {
	//lcd_fillScreen(BLACK);
	draw_title("AUDIO SAMPLER");

	lcd_drawRect(STATUS_X0, STATUS_Y0, STATUS_X1, STATUS_Y1, LIGHT_GRAY);
	text1x(16U, 46U, "SAMPLE RATE", LIGHT_GRAY, BLACK);
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
		p = put_text(text, "ACTIVE, designed for fs = ");
		p = put_uint(p, mixer_fs());
		(void)put_text(p, " Hz");
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
	p = put_text(p, " Hz  LATE ");
	p = put_uint(p, audio_late_edges);
	p = put_text(p, "  CLIP ");
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
	}
}

// ---------------------------------------------------------------------------
// Spectrum page (Ext B.1). Each frame: ask the ISR for a 128-point block
// (Sources/capture.c, about 49.9 kHz at every rate), run the FFT, and redraw only
// the part of each bar that changed. Bars cover 0 to 20 kHz, one per bin (390 Hz),
// 0 dB at the top (a full-scale sine) down to -80 dB. PF9 (J1101 pin 4) toggles
// once per frame, so a scope reads a square wave at half the frame rate
// ---------------------------------------------------------------------------
#define SPEC_X0				26U		// left edge of bar 0
#define SPEC_BAR_PITCH		4U		// 3 pixels of bar, 1 of gap
#define SPEC_BAR_W			3U
#define SPEC_MAX_BARS		53U		// (239 - SPEC_X0) / SPEC_BAR_PITCH
#define SPEC_TOP			64U
#define SPEC_BOT			247U	// bottom row of every bar
#define SPEC_H				(SPEC_BOT - SPEC_TOP + 1U)
#define SPEC_RANGE_DB10		800		// 80 dB from top to bottom
#define SPEC_SHOW_HZ		20000U	// the spec's 0 to 20 kHz

// 50 frames a second. The spec asks for at least 30, and this leaves margin at fx,
// where the ISR takes a big share of the CPU
#define SPEC_FRAME_CYCLES	(SYSCLK_HZ / 50U)
#define SPEC_TIMEOUT_CYCLES	(SYSCLK_HZ / 10U)	// a block takes 2.6 ms: 100 ms = no clock
#define SPEC_TEXT_CYCLES	(SYSCLK_HZ / 2U)	// numbers and labels twice a second

#define SPEC_LABEL_Y		(SPEC_BOT + 4U)
#define SPEC_INFO_Y			38U
#define SPEC_PEAK_Y			50U
#define SPEC_FPS_Y			262U

static int32_t spec_db10[FFT_HALF];
static uint16_t spec_drawn[SPEC_MAX_BARS];	// bar heights on screen now
static uint32_t spec_bars;					// how many bars cover 0 to 20 kHz
static uint32_t spec_fs_hz;					// capture rate of the last block
static uint32_t spec_peak_hz;
static int32_t spec_peak_db10;
static int spec_waiting;					// 1 = a block has been asked for
static uint32_t spec_wait_start;
static uint32_t spec_next_frame;
static uint32_t spec_frames;				// frames since the last text update
static uint32_t spec_text_tick;
static uint32_t spec_fps;

// PF9 = J1101 pin 4, push-pull, 50 MHz. dbg_init() has already clocked port F
static void frame_pin_init(void) {
	GPIOF->BSRR = GPIO_BSRR_BR9;
	GPIOF->CRH = (GPIOF->CRH & ~(GPIO_CRH_CNF9_Msk | GPIO_CRH_MODE9_Msk))
	           | (3U << GPIO_CRH_MODE9_Pos);
}

// One BSRR write, so it can't undo a pin change the ISR makes on port F
static void frame_pin_toggle(void) {
	GPIOF->BSRR = (GPIOF->ODR & GPIO_ODR_ODR9) ? GPIO_BSRR_BR9 : GPIO_BSRR_BS9;
}

// Level (tenths of a dB) to bar height in pixels
static uint16_t spec_height(int32_t db10) {
	int32_t h = ((db10 + SPEC_RANGE_DB10) * (int32_t)SPEC_H) / SPEC_RANGE_DB10;

	if (h < 0) {
		h = 0;
	} else if (h > (int32_t)SPEC_H) {
		h = (int32_t)SPEC_H;
	}
	return (uint16_t)h;
}

// Grows or shrinks one bar from its old height to its new one. Only the
// difference is drawn, which is what keeps a frame fast
static void spec_draw_bar(uint32_t k, uint16_t h) {
	uint16_t x = (uint16_t)(SPEC_X0 + k * SPEC_BAR_PITCH);
	uint16_t old = spec_drawn[k];

	if (h > old) {
		lcd_fillRect(x, SPEC_BOT - h + 1U, x + SPEC_BAR_W - 1U, SPEC_BOT - old, CYAN);
	} else if (h < old) {
		lcd_fillRect(x, SPEC_BOT - old + 1U, x + SPEC_BAR_W - 1U, SPEC_BOT - h, BLACK);
	}
	spec_drawn[k] = h;
}

// Frequency labels under the bars. They move a little with the measured rate
static void spec_draw_labels(void) {
	static const char * const label[5] = { "0", "5k", "10k", "15k", "20k" };
	uint32_t i;
	uint32_t x;

	lcd_fillRect(0U, SPEC_LABEL_Y, 239U, SPEC_LABEL_Y + 7U, BLACK);
	if (spec_fs_hz == 0U) {
		return;
	}
	for (i = 0U; i < 5U; i++) {
		// Bar centre for this frequency: f x 128 / fs bins along
		x = SPEC_X0 + 1U + (i * 5000U * FFT_N * SPEC_BAR_PITCH) / spec_fs_hz;
		x = (x >= 6U) ? x - 6U : 0U;	// roughly centre the text
		if (x > 220U) {
			x = 220U;
		}
		text1x((uint16_t)x, SPEC_LABEL_Y, label[i], LIGHT_GRAY, BLACK);
	}
}

// "fs 49906 Hz  BIN 390 Hz", "PEAK 1015 Hz  -6 dB", "50 FRAMES/S  PF9 = J1101 pin 4"
static void spec_draw_text(void) {
	char text[44];
	char *p;
	int32_t db;

	lcd_fillRect(0U, SPEC_INFO_Y, 239U, SPEC_PEAK_Y + 7U, BLACK);
	if (spec_fs_hz == 0U) {
		text1x(8U, SPEC_INFO_Y, "NO CLOCK: no samples coming in", RED, BLACK);
	} else {
		p = put_text(text, "fs ");
		p = put_uint(p, spec_fs_hz);
		p = put_text(p, " Hz  BIN ");
		p = put_uint(p, (spec_fs_hz + FFT_N / 2U) / FFT_N);
		p = put_text(p, " Hz  x");
		(void)put_uint(p, capture_decimation());
		text1x(8U, SPEC_INFO_Y, text, WHITE, BLACK);

		db = (spec_peak_db10 < 0) ? -spec_peak_db10 : spec_peak_db10;
		p = put_text(text, "PEAK ");
		p = put_uint(p, spec_peak_hz);
		p = put_text(p, (spec_peak_db10 < 0) ? " Hz  -" : " Hz  ");
		p = put_uint(p, (uint32_t)(db / 10));
		(void)put_text(p, " dB");
		text1x(8U, SPEC_PEAK_Y, text, YELLOW, BLACK);
	}

	lcd_fillRect(0U, SPEC_FPS_Y, 239U, SPEC_FPS_Y + 7U, BLACK);
	p = put_uint(text, spec_fps);
	(void)put_text(p, " FRAMES/S  PF9 = J1101 pin 4");
	text1x(8U, SPEC_FPS_Y, text, (spec_fps >= 30U) ? GREEN : RED, BLACK);
}

static void draw_spectrum_page(void) {
	uint32_t k;

	lcd_fillScreen(BLACK);
	draw_title("SPECTRUM");

	// dB scale on the left and the baseline under the bars
	text1x(2U, SPEC_TOP, "0", LIGHT_GRAY, BLACK);
	text1x(2U, SPEC_TOP + SPEC_H / 2U - 4U, "-40", LIGHT_GRAY, BLACK);
	text1x(2U, SPEC_BOT - 7U, "-80", LIGHT_GRAY, BLACK);
	lcd_line(SPEC_X0 - 2U, SPEC_TOP, SPEC_X0 - 2U, SPEC_BOT, DARK_GRAY);
	lcd_line(SPEC_X0 - 2U, SPEC_BOT + 1U, 239U, SPEC_BOT + 1U, DARK_GRAY);

	draw_button(&btn_back, "BACK", WHITE, DARK_CYAN);

	for (k = 0U; k < SPEC_MAX_BARS; k++) {
		spec_drawn[k] = 0U;
	}
	spec_bars = 0U;
	spec_fs_hz = 0U;
	spec_waiting = 0;
	spec_frames = 0U;
	spec_fps = 0U;
	spec_next_frame = dbg_cycles();
	spec_text_tick = spec_next_frame;
}

// Turns the finished block into bars
static void spec_frame(void) {
	uint32_t span = capture_span_cycles();
	uint32_t bars;
	uint32_t k;
	uint32_t peak = 1U;
	int32_t a;
	int32_t b;
	int32_t c;
	int32_t den;
	int32_t shift_q8 = 0;

	fft_spectrum(capture_data(), spec_db10);

	// The real capture rate of this block: 127 point periods took span cycles
	spec_fs_hz = (span != 0U) ? (uint32_t)(((uint64_t)(FFT_N - 1U) * SYSCLK_HZ) / span) : 0U;
	if (spec_fs_hz == 0U) {
		return;
	}

	// Bars for 0 to 20 kHz: bin k sits at k x fs / 128
	bars = (SPEC_SHOW_HZ * FFT_N) / spec_fs_hz + 1U;
	if (bars > SPEC_MAX_BARS) {
		bars = SPEC_MAX_BARS;
	}
	if (bars > FFT_HALF) {
		bars = FFT_HALF;
	}

	for (k = 0U; k < bars; k++) {
		spec_draw_bar(k, spec_height(spec_db10[k]));
	}
	// If the rate moved and there are fewer bars now, clear the extra ones
	for (k = bars; k < spec_bars; k++) {
		spec_draw_bar(k, 0U);
	}
	spec_bars = bars;

	// Loudest bin (skipping DC), then a parabola through it and its neighbours
	// to place the peak between bins. On a host test this was within 0.5 % from
	// 1 kHz up, but 20 % out at 200 to 500 Hz, where the tone sits within two bins
	// of DC. So it's a readout for the spectrum only, not B.2's pitch estimate
	for (k = 2U; k < bars - 1U; k++) {
		if (spec_db10[k] > spec_db10[peak]) {
			peak = k;
		}
	}
	a = spec_db10[peak - 1U];
	b = spec_db10[peak];
	c = spec_db10[peak + 1U];
	den = a - 2 * b + c;
	if (den != 0) {
		shift_q8 = (128 * (a - c)) / den;	// -128 to 128 = half a bin either way
	}
	spec_peak_hz = (uint32_t)((((int32_t)(peak * 256U) + shift_q8) * (int32_t)spec_fs_hz) / (int32_t)(FFT_N * 256U));
	spec_peak_db10 = b;
}

// Called every pass of the main loop while the spectrum page is up
static void spectrum_poll(void) {
	uint32_t now = dbg_cycles();
	uint32_t rate;

	if (spec_waiting) {
		if (capture_ready()) {
			spec_waiting = 0;
			spec_frame();
			frame_pin_toggle();
			spec_frames++;
		} else if ((now - spec_wait_start) > SPEC_TIMEOUT_CYCLES) {
			spec_waiting = 0;	// no clock: try again next frame
			spec_fs_hz = 0U;
		}
	} else if ((int32_t)(now - spec_next_frame) >= 0) {
		// Start the next frame on schedule. If a frame ran very late, start
		// counting again from now instead of rushing to catch up
		spec_next_frame += SPEC_FRAME_CYCLES;
		if ((now - spec_next_frame) < 0x80000000U) {
			spec_next_frame = now + SPEC_FRAME_CYCLES;
		}

		// The measured rate picks the decimation (stats_update, every 0.5 s).
		// Before the first measurement, use the selected rate
		rate = (stat_rate_hz != 0U) ? stat_rate_hz : rate_hz(rate_get());
		capture_start(rate);
		spec_waiting = 1;
		spec_wait_start = now;
	}

	// Twice a second: frame rate, the numbers, and the frequency labels
	if ((now - spec_text_tick) >= SPEC_TEXT_CYCLES) {
		spec_fps = (uint32_t)(((uint64_t)spec_frames * SYSCLK_HZ) / (now - spec_text_tick));
		spec_frames = 0U;
		spec_text_tick = now;
		spec_draw_text();
		spec_draw_labels();
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
		draw_spectrum_page();
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

	// Spectrum page: FFT tables (double maths, once) and the frame-rate pin
	fft_init();
	frame_pin_init();

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

	if (page == PAGE_SPECTRUM) {
		spectrum_poll();
	}

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
