#include "../Headers/stm32f103xe.h"
#include "../LCD/ili9341.h"
#include "../LCD/lcd_hw.h"
#include "../LCD/lcd_grph.h"
#include "clock.h"
#include "debug.h"
#include "adc.h"
#include "dac.h"
#include "extclk.h"
#include "tests.h"

// Half a second in CPU cycles. LED0 toggles on this, so it only flashes once a
// second if the CPU really runs at 72 MHz
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

		// Show the results, then flash LED0 so we can check the clock
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
		//dbg_led_toggle();	// off while debugging, so LED0 pauses during the beep
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

	extclk_init();
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
