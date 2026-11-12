#include "../Headers/stm32f103xe.h"
#include "../LCD/ili9341.h"
#include "../LCD/lcd_hw.h"
#include "../LCD/lcd_grph.h"
#include "clock.h"
#include "debug.h"
#include "adc.h"
#include "dac.h"
#include "extclk.h"
#include "audio.h"
#include "spi/clock_spi.h"
#include "ui/rate_ctrl.h"
#include "ui/mixer.h"
#include "Sampling/sampling.h"
#include "capture.h"
#include "tests.h"

// Half a second in CPU cycles. The heartbeat LED (LED5) toggles on this, so it only
// flashes once a second if the CPU really runs at 72 MHz
#define HALF_SECOND_CYCLES	(SYSCLK_HZ / 2U)

// For turning ADC codes into millivolts
#define ADC_MAX_CODE	4095U
#define ADC_VREF_MV		3300U

// The DAC test holds each output for 3 s, which is 6 half-second steps
#define HOLD_TOGGLES	6U

// The test tone. 2 kHz is near the speaker's loudest point, and 32 points per wave
// means 64000 DAC updates a second, or one every 1125 CPU cycles
#define TONE_HZ				2000U
#define TONE_POINTS			32U
#define TONE_UPDATE_CYCLES	(SYSCLK_HZ / (TONE_HZ * TONE_POINTS))
#define TONE_UPDATES_PER_TOGGLE	((TONE_HZ * TONE_POINTS) / 2U)

// One wave of the sine: 2048 + 1000 x sin(2 x pi x i / 32). It stays well inside
// the range the DAC can reach
static const uint16_t tone_table[TONE_POINTS] = {
	2048, 2243, 2431, 2604, 2755, 2879, 2972, 3029,
	3048, 3029, 2972, 2879, 2755, 2604, 2431, 2243,
	2048, 1853, 1665, 1492, 1341, 1217, 1124, 1067,
	1048, 1067, 1124, 1217, 1341, 1492, 1665, 1853
};

// Cycle count at the end of the last wait, so timing doesn't drift
static uint32_t last_tick;

// Prints a label and a number on one line. The number is always 10 characters
// wide, so a shorter value fully covers the longer one before it
static void lcd_put_value(unsigned short y, const char *label, uint32_t value, uint32_t base) {
	char text[11];	// 10 decimal digits of a uint32_t, plus the terminator
	int i = sizeof(text) - 1;

	// Build the digits from the right, then fill the rest with spaces
	text[i] = '\0';
	do {
		text[--i] = "0123456789ABCDEF"[value % base];
		value /= base;
	} while (value != 0U);
	while (i > 0) {
		text[--i] = ' ';
	}

	lcd_putString(10, y, (unsigned char *)label);
	lcd_putString(110, y, (unsigned char *)text);
}

// Converts an ADC code to millivolts
static uint32_t code_to_mv(uint32_t code) {
	return (code * ADC_VREF_MV) / ADC_MAX_CODE;
}

// Stage 2: reads the ADC flat out for half a second, shows the stats, and repeats
void test_adc(void) {
	uint32_t start;
	uint32_t code;
	uint32_t min;
	uint32_t max;
	uint32_t sum;
	uint32_t count;
	uint32_t conv_cycles = 0U;

	adc_init();
	lcd_putString(10, 10, (unsigned char *)"ELEC4123 STAGE 2 ADC TEST");
	lcd_put_value(30, "SYSCLK (Hz)", SystemCoreClock, 10U);

	last_tick = dbg_cycles();
	for (;;) {
		// Reset the stats for this half second
		min = ADC_MAX_CODE;
		max = 0U;
		sum = 0U;
		count = 0U;

		// Take readings until the half second is up. Sum can't overflow in that time
		while ((dbg_cycles() - last_tick) < HALF_SECOND_CYCLES) {
			start = dbg_cycles();
			dbg_scope_high();		// PF8 stays high for one whole conversion
			adc_start();
			while (!adc_is_done()) {
			}
			dbg_scope_low();
			conv_cycles = dbg_cycles() - start;

			// Update the lowest, highest and running total
			code = adc_result();
			if (code < min) {
				min = code;
			}
			if (code > max) {
				max = code;
			}
			sum += code;
			count++;
		}
		last_tick += HALF_SECOND_CYCLES;

		// Show the results, then flash LED5 so we can check the clock
		lcd_put_value(50, "AVERAGE (code)", sum / count, 10U);
		lcd_put_value(65, "MIN (code)", min, 10U);
		lcd_put_value(80, "MAX (code)", max, 10U);
		lcd_put_value(95, "SWING (code)", max - min, 10U);
		lcd_put_value(115, "AVERAGE (mV)", code_to_mv(sum / count), 10U);
		lcd_put_value(130, "SWING (mV)", code_to_mv(max - min), 10U);
		lcd_put_value(150, "CONV CYCLES", conv_cycles, 10U);
		lcd_put_value(165, "CONVERSIONS", count, 10U);

		dbg_led_toggle();
	}
}

// Waits until the given number of cycles has passed since the last wait
static void wait_cycles(uint32_t cycles) {
	while ((dbg_cycles() - last_tick) < cycles) {
	}
	last_tick += cycles;	// adding here, not re-reading, stops timing drifting
}

// Stage 3: holds one voltage for 3 s so we can measure it at J1301
static void dac_hold_level(uint32_t target_mv) {
	uint32_t code = (target_mv * DAC_MAX_CODE) / DAC_VREF_MV;
	uint32_t toggles;

	dac_write(code);
	lcd_putString(10, 50, (unsigned char *)"OUTPUT: FIXED VOLTAGE");
	lcd_put_value(65, "TARGET (mV)", target_mv, 10U);
	lcd_put_value(80, "DAC CODE", code, 10U);

	for (toggles = 0U; toggles < HOLD_TOGGLES; toggles++) {
		wait_cycles(HALF_SECOND_CYCLES);
		//dbg_led_toggle();
	}
}

// Stage 3: plays the sine for 3 s. PF8 runs as a 2 kHz square wave in step with
// it, which gives the scope something clean to trigger on
static void dac_play_tone(void) {
	uint32_t phase = 0U;
	uint32_t toggles;
	uint32_t updates;

	lcd_putString(10, 50, (unsigned char *)"OUTPUT: 2 KHZ TONE   ");
	lcd_put_value(65, "TARGET (mV)", 0U, 10U);
	lcd_put_value(80, "DAC CODE", 0U, 10U);

	// The screen writes took time, so restart the timing, or the first few updates
	// would rush to catch up
	last_tick = dbg_cycles();

	for (toggles = 0U; toggles < HOLD_TOGGLES; toggles++) {
		for (updates = 0U; updates < TONE_UPDATES_PER_TOGGLE; updates++) {
			wait_cycles(TONE_UPDATE_CYCLES);
			dac_write(tone_table[phase]);

			// PF8 goes high at the start of each wave and low halfway through
			if (phase == 0U) {
				dbg_scope_high();
			} else if (phase == (TONE_POINTS / 2U)) {
				dbg_scope_low();
			}
			phase = (phase + 1U) % TONE_POINTS;
		}
		//dbg_led_toggle();	// off while debugging, so LED5 pauses during the beep
	}
}

// Stage 3: loops through the beep and five fixed voltages, 18 s in total
void test_dac(void) {
	dac_init();
	lcd_putString(10, 10, (unsigned char *)"ELEC4123 STAGE 3 DAC TEST");
	lcd_put_value(30, "SYSCLK (Hz)", SystemCoreClock, 10U);

	last_tick = dbg_cycles();
	for (;;) {
		dac_play_tone();
		dac_hold_level(500U);
		dac_hold_level(1000U);
		dac_hold_level(1650U);
		dac_hold_level(2500U);
		dac_hold_level(3000U);
	}
}

// Stage 4: counts clock edges over each half second to show the rate. The spec lets
// us measure with the CPU's timing, as long as sampling isn't timed from it
void test_extclk(void) {
	uint32_t edges;
	uint32_t last_edges;

	audio_init();
	lcd_putString(10, 10, (unsigned char *)"ELEC4123 STAGE 4 CLOCK TEST");
	lcd_put_value(30, "SYSCLK (Hz)", SystemCoreClock, 10U);

	last_edges = extclk_edges;
	last_tick = dbg_cycles();
	for (;;) {
		wait_cycles(HALF_SECOND_CYCLES);
		edges = extclk_edges;

		// Edges in this half second x 2 = rate. The subtraction survives a counter wrap
		lcd_put_value(50, "CLOCK RATE (Hz)", (edges - last_edges) * 2U, 10U);
		lcd_put_value(65, "TOTAL EDGES", edges, 10U);
		last_edges = edges;

		dbg_led_toggle();
	}
}

// Stage 5: audio runs in the ISR by itself. This loop only shows how it's doing:
// the rate, whether any samples went wrong, and how much of the budget the ISR uses
void test_audio(void) {
	uint32_t edges;
	uint32_t last_edges;
	uint32_t rate;
	uint32_t budget;
	uint32_t isr_cycles;

	audio_init();
	lcd_putString(10, 10, (unsigned char *)"ELEC4123 STAGE 5 PASSTHROUGH");
	lcd_put_value(30, "SYSCLK (Hz)", SystemCoreClock, 10U);

	last_edges = extclk_edges;
	last_tick = dbg_cycles();
	for (;;) {
		wait_cycles(HALF_SECOND_CYCLES);

		// Work out the rate, and the CPU cycles we get per sample at that rate
		edges = extclk_edges;
		rate = (edges - last_edges) * 2U;
		last_edges = edges;
		budget = (rate != 0U) ? (SYSCLK_HZ / rate) : 0U;

		// Take the longest ISR from this half second, then start tracking again
		isr_cycles = audio_max_cycles;
		audio_max_cycles = 0U;

		lcd_put_value(50, "CLOCK RATE (Hz)", rate, 10U);
		lcd_put_value(65, "TOTAL EDGES", edges, 10U);
		lcd_put_value(85, "OVERRUNS", audio_overruns, 10U);
		lcd_put_value(100, "LATE EDGES", audio_late_edges, 10U);
		lcd_put_value(120, "ISR CYCLES", isr_cycles, 10U);
		lcd_put_value(135, "BUDGET (cycles)", budget, 10U);
		lcd_put_value(150, "ISR LOAD (%)", (budget != 0U) ? ((isr_cycles * 100U) / budget) : 0U, 10U);

		dbg_led_toggle();
	}
}

// Stage 6: the commands to send, in order. The bad ones sit right after a valid
// rate, so the rate shown next to them should stay the same
typedef struct {
	uint8_t cmd;
	const char *label;
} spi_step_t;

static const spi_step_t spi_steps[] = {
	{ CMD_ENABLE,    "E1 ENABLE" },
	{ CMD_RATE_FX,   "D1 FX" },
	{ CMD_RATE_FX_2, "D2 FX/2" },
	{ CMD_RATE_FX_4, "D4 FX/4" },
	{ CMD_RATE_FX_8, "D8 FX/8" },
	{ 0x55U,         "55 BAD" },
	{ 0xD3U,         "D3 BAD" },
	{ 0xFFU,         "FF BAD" },
	{ 0x00U,         "00 BAD" },
	{ CMD_DISABLE,   "E0 DISABLE" },
	{ 0xE2U,         "E2 BAD" },
	{ CMD_ENABLE,    "E1 ENABLE" }
};

#define SPI_STEP_COUNT	(sizeof(spi_steps) / sizeof(spi_steps[0]))

// Stage 6: sends each command, then measures the clock that comes back on PF11.
// No UI involved, so it tests the SPI link and the clock board by themselves
void test_spi(void) {
	uint32_t i;
	uint32_t edges;

	audio_init();
	clock_spi_init();
	lcd_putString(10, 10, (unsigned char *)"ELEC4123 STAGE 6 SPI TEST");
	lcd_put_value(30, "SYSCLK (Hz)", SystemCoreClock, 10U);

	last_tick = dbg_cycles();
	for (;;) {
		for (i = 0U; i < SPI_STEP_COUNT; i++) {
			clock_spi_send(spi_steps[i].cmd);

			// Give the board half a second to settle, then count edges for half a second
			wait_cycles(HALF_SECOND_CYCLES);
			edges = extclk_edges;
			wait_cycles(HALF_SECOND_CYCLES);

			// One line per command: what we sent, and the rate we measured after it
			lcd_put_value((unsigned short)(50U + i * 14U), spi_steps[i].label,
			              (extclk_edges - edges) * 2U, 10U);
			dbg_led_toggle();
		}
	}
}

// SPI check without the clock board. With a jumper from MOSI to MISO (J901 pin 5 to
// pin 3), every byte we send comes straight back. Sends 0 to 255 over and over and
// counts any byte that comes back different
void test_spi_loopback(void) {
	uint32_t value = 0U;
	uint32_t sent = 0U;
	uint32_t errors = 0U;
	uint32_t window_errors;
	uint8_t echo = 0U;

	clock_spi_init();
	lcd_putString(10, 10, (unsigned char *)"ELEC4123 SPI LOOPBACK TEST");
	lcd_putString(10, 22, (unsigned char *)"Jumper J901 pin 5 to pin 3");
	lcd_put_value(40, "SYSCLK (Hz)", SystemCoreClock, 10U);

	last_tick = dbg_cycles();
	for (;;) {
		// Send bytes for half a second, checking each one as it comes back
		window_errors = 0U;
		while ((dbg_cycles() - last_tick) < HALF_SECOND_CYCLES) {
			echo = clock_spi_send((uint8_t)value);
			if (echo != (uint8_t)value) {
				window_errors++;
			}
			value = (value + 1U) & 0xFFU;
			sent++;
		}
		last_tick += HALF_SECOND_CYCLES;
		errors += window_errors;

		// The last pair should match, and errors should stay at 0
		lcd_put_value(60, "LAST SENT (hex)", (value - 1U) & 0xFFU, 16U);
		lcd_put_value(75, "LAST ECHO (hex)", echo, 16U);
		lcd_put_value(95, "BYTES SENT", sent, 10U);
		lcd_put_value(110, "ERRORS", errors, 10U);

		if (window_errors == 0U) {
			lcd_fontColor(GREEN, BLACK);
			lcd_putString(10, 130, (unsigned char *)"PASS: every byte came back   ");
		} else {
			lcd_fontColor(RED, BLACK);
			lcd_putString(10, 130, (unsigned char *)"FAIL: check the jumper       ");
		}
		lcd_fontColor(WHITE, BLACK);

		dbg_led_toggle();
	}
}

// ---------------------------------------------------------------------------
// Helpers for the Stage 7 and 8 tests
// ---------------------------------------------------------------------------

// Interrupt entry and exit, which the ISR's own cycle count can't see (Stage 5)
#define ISR_ENTRY_EXIT_CYCLES	30U

// Characters per LCD line at x = 10. Lines are padded to this so a shorter line
// fully covers a longer one left from the last pass
#define LINE_CHARS		38U

// Line buffers are bigger than a line, because a line with four 10-digit counters
// can reach 67 characters. put_line() cuts it back to LINE_CHARS
#define TEXT_BUF		80U

// Writes v in decimal at p and returns the new end of the string
static char *fmt_uint(char *p, uint32_t v) {
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

static char *fmt_text(char *p, const char *s) {
	while (*s != '\0') {
		*p++ = *s++;
	}
	*p = '\0';
	return p;
}

// Pads (or cuts) the line to LINE_CHARS and draws it in the given colour. text
// must be a TEXT_BUF buffer
static void put_line(unsigned short y, char *text, lcd_color_t colour) {
	uint32_t n = 0U;

	while ((text[n] != '\0') && (n < LINE_CHARS)) {
		n++;
	}
	while (n < LINE_CHARS) {
		text[n++] = ' ';
	}
	text[n] = '\0';

	lcd_fontColor(colour, BLACK);
	lcd_putString(10, y, (unsigned char *)text);
	lcd_fontColor(WHITE, BLACK);
}

// Starts timing from now. Call after anything slow (LCD, SPI), or wait_cycles()
// would think it's late and return straight away
static void restart_timing(void) {
	last_tick = dbg_cycles();
}

// Counts edges for the given number of cycles and returns the rate in Hz
static uint32_t measure_rate(uint32_t cycles) {
	uint32_t edges = extclk_edges;

	wait_cycles(cycles);
	return (uint32_t)(((uint64_t)(extclk_edges - edges) * SYSCLK_HZ) / cycles);
}

// 1 if a and b are within 2 % of each other (and a isn't 0)
static int within_2pc(uint32_t a, uint32_t b) {
	uint32_t diff = (a > b) ? (a - b) : (b - a);

	return (a != 0U) && (diff <= a / 50U);
}

static const char * const rate_cmd_text[RATE_COUNT] = { "D1", "D2", "D4", "D8" };

// ---------------------------------------------------------------------------
// Stage 7: FFT capture test (ACTIVE_TEST 11)
// ---------------------------------------------------------------------------

// The plot: one pixel column per captured point, 128 pixels tall
#define PLOT_X0			56U
#define PLOT_Y0			130U
#define PLOT_H			128U
#define CAPTURE_STEP_CYCLES	(4U * SYSCLK_HZ)	// 4 s on each rate
#define CAPTURE_TIMEOUT		(SYSCLK_HZ / 10U)	// a block takes 2.6 ms, so 100 ms = no clock

// Point value (0 to 32760) to a screen row, top of the plot = full scale
static unsigned short plot_y(uint32_t v) {
	return (unsigned short)(PLOT_Y0 + (PLOT_H - 1U) - (v * (PLOT_H - 1U)) / CAPTURE_FULL_SCALE);
}

// Steps through the four rates. On each one it measures the clock, grabs blocks
// over and over and draws them. The capture rate should read about 49.9 kHz at
// every rate, and a steady tone should look the same width at all four
void test_capture(void) {
	rate_t r = RATE_FX;
	uint32_t rate;
	uint32_t step_start;
	uint32_t wait_start;
	uint32_t i;
	uint32_t v;
	uint32_t min;
	uint32_t max;
	uint32_t span;
	const uint16_t *data;
	char text[TEXT_BUF];
	char *p;

	audio_init();
	rate_init();
	lcd_putString(10, 10, (unsigned char *)"ELEC4123 STAGE 7 FFT CAPTURE");
	lcd_putString(10, 22, (unsigned char *)"Tone into J1302, rate changes every 4 s");

	for (;;) {
		rate_set(r);

		// Let the board settle, then measure the real rate. That picks the
		// decimation, the same way the FFT page will
		restart_timing();
		wait_cycles(SYSCLK_HZ / 5U);
		rate = measure_rate(SYSCLK_HZ / 10U);

		p = fmt_text(text, rate_cmd_text[r]);
		p = fmt_text(p, " MEASURED ");
		p = fmt_uint(p, rate);
		(void)fmt_text(p, " Hz");
		put_line(40, text, WHITE);

		step_start = dbg_cycles();
		while ((dbg_cycles() - step_start) < CAPTURE_STEP_CYCLES) {
			// Ask for a block and wait for it. 128 points at 49.9 kHz is 2.6 ms
			capture_start(rate);
			wait_start = dbg_cycles();
			while (!capture_ready() && ((dbg_cycles() - wait_start) < CAPTURE_TIMEOUT)) {
			}
			if (!capture_ready()) {
				(void)fmt_text(text, "NO BLOCK: is the clock running?");
				put_line(55, text, RED);
				continue;
			}

			// The block's own stats
			data = capture_data();
			min = CAPTURE_FULL_SCALE;
			max = 0U;
			for (i = 0U; i < CAPTURE_POINTS; i++) {
				if (data[i] < min) {
					min = data[i];
				}
				if (data[i] > max) {
					max = data[i];
				}
			}
			span = capture_span_cycles();

			// Samples per point, and the real capture rate: 127 point periods
			// took span cycles
			p = fmt_text(text, "SAMPLES PER POINT ");
			(void)fmt_uint(p, capture_decimation());
			put_line(55, text, WHITE);

			p = fmt_text(text, "CAPTURE RATE ");
			p = fmt_uint(p, (span != 0U) ? (uint32_t)(((uint64_t)(CAPTURE_POINTS - 1U) * SYSCLK_HZ) / span) : 0U);
			(void)fmt_text(p, " Hz (want ~49906)");
			put_line(70, text, WHITE);

			p = fmt_text(text, "MIN ");
			p = fmt_uint(p, min);
			p = fmt_text(p, "  MAX ");
			p = fmt_uint(p, max);
			p = fmt_text(p, "  (mid ");
			p = fmt_uint(p, CAPTURE_MID);
			(void)fmt_text(p, ")");
			put_line(85, text, WHITE);

			p = fmt_text(text, "OVERRUNS ");
			p = fmt_uint(p, audio_overruns);
			p = fmt_text(p, "  LATE EDGES ");
			(void)fmt_uint(p, audio_late_edges);
			put_line(100, text, WHITE);

			// Wipe the old trace, draw the midline, then the new trace
			lcd_fillRect(PLOT_X0, PLOT_Y0, PLOT_X0 + CAPTURE_POINTS - 1U, PLOT_Y0 + PLOT_H - 1U, BLACK);
			lcd_line(PLOT_X0, plot_y(CAPTURE_MID), PLOT_X0 + CAPTURE_POINTS - 1U, plot_y(CAPTURE_MID), DARK_GRAY);
			for (i = 1U; i < CAPTURE_POINTS; i++) {
				v = data[i];
				lcd_line((unsigned short)(PLOT_X0 + i - 1U), plot_y(data[i - 1U]),
				         (unsigned short)(PLOT_X0 + i), plot_y(v), GREEN);
			}
			dbg_led_toggle();
		}

		r = (rate_t)(((uint32_t)r + 1U) % RATE_COUNT);
	}
}

// ---------------------------------------------------------------------------
// Stage 8: rate sweep proof (ACTIVE_TEST 12)
// ---------------------------------------------------------------------------

// Straight after each command, the rate is counted in 20 windows of 10 ms. The
// settle time is when it last moved more than 5 % away from the steady rate
#define SETTLE_WINDOWS			20U
#define SETTLE_WINDOW_CYCLES	(SYSCLK_HZ / 100U)

// Never switch the mixer on above this measured rate, even if we asked for fx/8.
// Halfway (log scale) between fx/8 and fx/4. Protects the test when a signal
// generator is running at a fast rate and ignoring our commands
#define MIXER_MAX_HZ			CAPTURE_SPLIT_HZ

#define SWEEP_ROW_Y0			40U
#define SWEEP_ROW_PITCH			34U

// Waits the given number of cycles, re-arming the FFT capture each time a block
// fills. That way the ISR's worst case includes the capture work the FFT page adds
static void wait_with_capture(uint32_t cycles, uint32_t rate_hz) {
	uint32_t start = dbg_cycles();

	capture_start(rate_hz);
	while ((dbg_cycles() - start) < cycles) {
		if (capture_ready()) {
			capture_start(rate_hz);
		}
	}
}

// One rate: send the command, watch it settle, then run one steady second with
// the capture on (and the mixer on at fx/8). Three lines on the LCD. Returns the
// steady rate
static uint32_t sweep_rate(rate_t r, unsigned short y) {
	uint32_t window[SETTLE_WINDOWS];
	uint32_t ovr0;
	uint32_t late0;
	uint32_t switch_ovr;
	uint32_t switch_late;
	uint32_t run_ovr;
	uint32_t run_late;
	uint32_t edges;
	uint32_t now;
	uint32_t start;
	uint32_t elapsed;
	uint32_t rate;
	uint32_t expect;
	uint32_t tol;
	uint32_t settle;
	uint32_t budget;
	uint32_t isr;
	uint32_t i;
	char text[TEXT_BUF];
	char *p;

	// The mixer only fits at fx/8. Off before a faster clock can arrive, the
	// same order the UI uses
	if (r != RATE_FX_8) {
		mixer_set_active(0);
	}

	// The switch: anything that goes wrong between the command and 200 ms later
	ovr0 = audio_overruns;
	late0 = audio_late_edges;
	rate_set(r);
	restart_timing();
	edges = extclk_edges;
	for (i = 0U; i < SETTLE_WINDOWS; i++) {
		wait_cycles(SETTLE_WINDOW_CYCLES);
		now = extclk_edges;
		window[i] = now - edges;
		edges = now;
	}
	switch_ovr = audio_overruns - ovr0;
	switch_late = audio_late_edges - late0;

	// Mixer on at fx/8, but only if the clock really is slow
	rate = window[SETTLE_WINDOWS - 1U] * 100U;
	if ((r == RATE_FX_8) && (rate != 0U) && (rate < MIXER_MAX_HZ)) {
		mixer_set_active(1);
	}

	// The steady second. Clear the ISR maximum first, so it only covers this second
	ovr0 = audio_overruns;
	late0 = audio_late_edges;
	edges = extclk_edges;
	audio_max_cycles = 0U;
	start = dbg_cycles();
	wait_with_capture(SYSCLK_HZ, rate);
	elapsed = dbg_cycles() - start;
	rate = (uint32_t)((((uint64_t)(extclk_edges - edges) * SYSCLK_HZ) + elapsed / 2U) / elapsed);
	isr = audio_max_cycles + ISR_ENTRY_EXIT_CYCLES;
	run_ovr = audio_overruns - ovr0;
	run_late = audio_late_edges - late0;

	// Settle time: walk back from the last window while each one is within 5 %
	// of the steady rate. settle = the first window of that final good run
	expect = rate / 100U;
	tol = expect / 20U + 1U;
	settle = SETTLE_WINDOWS;
	for (i = SETTLE_WINDOWS; i > 0U; i--) {
		if ((window[i - 1U] + tol < expect) || (window[i - 1U] > expect + tol)) {
			break;
		}
		settle = i - 1U;
	}

	// Line 1: "D8  49906 Hz   settled in <=10 ms"
	p = fmt_text(text, rate_cmd_text[r]);
	p = fmt_text(p, "  ");
	p = fmt_uint(p, rate);
	p = fmt_text(p, " Hz");
	if (rate == 0U) {
		(void)fmt_text(p, "   NO CLOCK");
	} else if (settle >= SETTLE_WINDOWS) {
		(void)fmt_text(p, "   not settled by 200 ms");
	} else {
		p = fmt_text(p, "   settled in <=");
		p = fmt_uint(p, (settle + 1U) * 10U);
		(void)fmt_text(p, " ms");
	}
	put_line(y, text, WHITE);

	// Line 2: "  ISR 412 of 1443 cycles = 28%"
	budget = (rate != 0U) ? (SYSCLK_HZ / rate) : 0U;
	p = fmt_text(text, "  ISR ");
	p = fmt_uint(p, isr);
	p = fmt_text(p, " of ");
	p = fmt_uint(p, budget);
	p = fmt_text(p, " cycles = ");
	p = fmt_uint(p, (budget != 0U) ? ((isr * 100U) / budget) : 0U);
	p = fmt_text(p, "%");
	if (mixer_is_active()) {
		(void)fmt_text(p, " +MIX");
	}
	put_line((unsigned short)(y + 10U), text, ((budget != 0U) && (isr >= budget)) ? RED : WHITE);

	// Line 3: "  OVR/LATE  switch 0/0  run 0/0"
	p = fmt_text(text, "  OVR/LATE  switch ");
	p = fmt_uint(p, switch_ovr);
	p = fmt_text(p, "/");
	p = fmt_uint(p, switch_late);
	p = fmt_text(p, "  run ");
	p = fmt_uint(p, run_ovr);
	p = fmt_text(p, "/");
	(void)fmt_uint(p, run_late);
	put_line((unsigned short)(y + 20U), text,
	         ((switch_ovr | switch_late | run_ovr | run_late) != 0U) ? RED : GREEN);

	return rate;
}

// Disable, invalid while off, enable, invalid while on. The rate has to stop, stay
// stopped through the bad byte, come back to the same rate, and survive the bad
// byte while running. Two lines on the LCD
static void sweep_hold(uint32_t rate_before, unsigned short y) {
	uint32_t off_rate;
	uint32_t off_bad_rate;
	uint32_t on_rate;
	uint32_t on_bad_rate;
	int pass;
	char text[TEXT_BUF];
	char *p;

	rate_enable(0);
	restart_timing();
	wait_cycles(SYSCLK_HZ / 5U);
	off_rate = measure_rate(SYSCLK_HZ / 2U);

	rate_send_invalid();
	restart_timing();
	wait_cycles(SYSCLK_HZ / 5U);
	off_bad_rate = measure_rate(SYSCLK_HZ / 2U);

	rate_enable(1);
	restart_timing();
	wait_cycles(SYSCLK_HZ / 5U);
	on_rate = measure_rate(SYSCLK_HZ);

	rate_send_invalid();
	restart_timing();
	wait_cycles(SYSCLK_HZ / 5U);
	on_bad_rate = measure_rate(SYSCLK_HZ);

	pass = (off_rate == 0U) && (off_bad_rate == 0U) &&
	       within_2pc(rate_before, on_rate) && within_2pc(on_rate, on_bad_rate);

	// "E0 0 Hz   then D3 0 Hz"
	p = fmt_text(text, "E0 ");
	p = fmt_uint(p, off_rate);
	p = fmt_text(p, " Hz   then D3 ");
	p = fmt_uint(p, off_bad_rate);
	(void)fmt_text(p, " Hz");
	put_line(y, text, WHITE);

	// "E1 49906 Hz then D3 49906 Hz PASS"
	p = fmt_text(text, "E1 ");
	p = fmt_uint(p, on_rate);
	p = fmt_text(p, " Hz then D3 ");
	p = fmt_uint(p, on_bad_rate);
	(void)fmt_text(p, pass ? " Hz PASS" : " Hz FAIL");
	put_line((unsigned short)(y + 10U), text, pass ? GREEN : RED);
}

// Stage 8: goes through D1, D2, D4, D8, then the disable / invalid / enable checks,
// over and over. Each pass gives, for every rate: the measured rate, how long it
// took to settle, the worst ISR against the budget, and any overruns or late edges
// during the switch and during a steady second. Needs the clock board: a signal
// generator ignores the commands, so every row shows its frequency and the hold
// check fails
void test_rates(void) {
	rate_t r;
	uint32_t pass_count = 0U;
	uint32_t ovr0;
	uint32_t late0;
	uint32_t fx8_rate = 0U;
	char text[TEXT_BUF];
	char *p;

	audio_init();
	sampling_init();
	mixer_init(rate_hz(RATE_FX_8));
	rate_init();
	lcd_putString(10, 10, (unsigned char *)"ELEC4123 STAGE 8 RATE SWEEP");
	lcd_putString(10, 22, (unsigned char *)"OVR = repeated, LATE = lost edge risk");

	for (;;) {
		ovr0 = audio_overruns;
		late0 = audio_late_edges;

		for (r = RATE_FX; r < RATE_COUNT; r++) {
			fx8_rate = sweep_rate(r, (unsigned short)(SWEEP_ROW_Y0 + (uint32_t)r * SWEEP_ROW_PITCH));
			dbg_led_toggle();
		}
		sweep_hold(fx8_rate, (unsigned short)(SWEEP_ROW_Y0 + RATE_COUNT * SWEEP_ROW_PITCH));
		pass_count++;

		// Totals since power-up, and this pass's verdict
		p = fmt_text(text, "PASS ");
		p = fmt_uint(p, pass_count);
		p = fmt_text(p, "  TOTAL OVR ");
		p = fmt_uint(p, audio_overruns);
		p = fmt_text(p, " LATE ");
		(void)fmt_uint(p, audio_late_edges);
		put_line(200, text, WHITE);

		p = fmt_text(text, "EDGES ");
		(void)fmt_uint(p, extclk_edges);
		put_line(212, text, WHITE);

		if ((audio_overruns == ovr0) && (audio_late_edges == late0)) {
			(void)fmt_text(text, "NO DROPPED OR REPEATED SAMPLES");
			put_line(228, text, GREEN);
		} else {
			(void)fmt_text(text, "SAMPLES WENT WRONG THIS PASS");
			put_line(228, text, RED);
		}
	}
}

// ---------------------------------------------------------------------------
// Stage 8: ramp staircase (ACTIVE_TEST 13)
// ---------------------------------------------------------------------------

// The DAC steps up by one stair per clock edge: 64 stairs of 48 codes (39 mV),
// 512 to 3536, which stays inside what the DAC can reach
#define RAMP_STEPS			64U
#define RAMP_LOW			512U
#define RAMP_STEP_CODES		48U
#define RAMP_RATE_CYCLES	(5U * SYSCLK_HZ)	// 5 s on each rate

static uint32_t ramp_index = 0U;

// Installed in audio_process, so it runs in the ISR in place of passthrough. It
// ignores the input and outputs the next stair. A repeated sample shows on the
// scope as a stair twice as wide, so every stair must be exactly one clock long
static uint32_t ramp_process(uint32_t code) {
	(void)code;
	ramp_index = (ramp_index + 1U) & (RAMP_STEPS - 1U);
	return RAMP_LOW + ramp_index * RAMP_STEP_CODES;
}

// Plays the staircase at each rate for 5 s. Scope CH1 on J1101 pin 6 (the clock),
// CH2 on J1301 (the DAC). Turn the volume knob down: the speaker plays it as a
// loud buzz (fx / 64 = 6.2 kHz down to 780 Hz)
void test_ramp(void) {
	rate_t r = RATE_FX;
	uint32_t step_start;
	uint32_t rate;
	char text[TEXT_BUF];
	char *p;

	audio_init();
	audio_process = ramp_process;	// one aligned write: the ISR sees all or nothing
	rate_init();
	lcd_putString(10, 10, (unsigned char *)"ELEC4123 STAGE 8 RAMP TEST");
	lcd_putString(10, 22, (unsigned char *)"CH1 J1101 pin 6, CH2 J1301. Volume low!");

	for (;;) {
		rate_set(r);
		restart_timing();
		step_start = dbg_cycles();
		while ((dbg_cycles() - step_start) < RAMP_RATE_CYCLES) {
			rate = measure_rate(HALF_SECOND_CYCLES);

			p = fmt_text(text, rate_cmd_text[r]);
			p = fmt_text(p, "  ");
			p = fmt_uint(p, rate);
			(void)fmt_text(p, " Hz");
			put_line(45, text, WHITE);

			p = fmt_text(text, "RAMP ");
			p = fmt_uint(p, rate / RAMP_STEPS);
			(void)fmt_text(p, " Hz, one stair per edge");
			put_line(60, text, WHITE);

			p = fmt_text(text, "OVERRUNS ");
			p = fmt_uint(p, audio_overruns);
			p = fmt_text(p, "  LATE EDGES ");
			(void)fmt_uint(p, audio_late_edges);
			put_line(75, text, ((audio_overruns | audio_late_edges) != 0U) ? RED : GREEN);

			dbg_led_toggle();
		}
		r = (rate_t)(((uint32_t)r + 1U) % RATE_COUNT);
	}
}
