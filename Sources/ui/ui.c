#include "../../Headers/stm32f103xe.h"
#include "../../LCD/lcd_grph.h"
#include "../../LCD/font5x7.h"
#include "../clock.h"
#include "../debug.h"
#include "rate_ctrl.h"
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
static const box_t btn_enable   = {   8, 224, 231, 263 };
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
	// Clear the value line, because "99.81 kHz" is shorter than "399.25 kHz"
	lcd_fillRect(STATUS_X0 + 1U, 58U, STATUS_X1 - 1U, 73U, BLACK);
	text2x(16U, 58U, rate_label(rate_get()), WHITE, BLACK);

	// Same length either way, so one overwrites the other completely
	if (rate_is_enabled()) {
		text1x(16U, 82U, "CLOCK: RUNNING", GREEN, BLACK);
	} else {
		text1x(16U, 82U, "CLOCK: STOPPED", RED, BLACK);
	}
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

static void draw_enable_button(void) {
	if (rate_is_enabled()) {
		draw_button(&btn_enable, "STOP CLOCK", WHITE, RED);
	} else {
		draw_button(&btn_enable, "START CLOCK", BLACK, GREEN);
	}
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

	draw_button(&btn_mixer, "MIXER", WHITE, DARK_CYAN);
	draw_button(&btn_spectrum, "SPECTRUM", WHITE, DARK_CYAN);

	// Backup controls (touch also works)
	text1x(8U, 308U, "PB0: next rate  PB1: start/stop", DARK_GRAY, BLACK);
}

// ---------------------------------------------------------------------------
// Extension pages (placeholders until Ext A and Ext B exist)
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

static void show_page(page_t p) {
	page = p;
	switch (p) {
	case PAGE_MIXER:
		draw_placeholder_page("MIXER", "Ext A: 3-band mixer");
		break;
	case PAGE_SPECTRUM:
		draw_placeholder_page("SPECTRUM", "Ext B: FFT spectrum");
		break;
	default:
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
	rate_set(r);
	draw_status();
	draw_rate_buttons();
}

static void toggle_enable(void) {
	rate_enable(!rate_is_enabled());
	draw_status();
	draw_rate_buttons();
	draw_enable_button();
}

void ui_touch(uint16_t x, uint16_t y) {
	rate_t r;

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
