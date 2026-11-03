#include "../Headers/stm32f103xe.h"
#include "../LCD/ili9341.h"
#include "../LCD/lcd_hw.h"
#include "../LCD/lcd_grph.h"
#include "clock.h"
#include "debug.h"
#include "adc.h"
#include "dac.h"
#include "tests.h"

// Counted in CPU cycles, so LED0 flashes once a second only at 72 MHz
#define HALF_SECOND_CYCLES	(SYSCLK_HZ / 2U)

#define ADC_MAX_CODE	4095U
#define ADC_VREF_MV		3300U

// DAC test: each output is held for 3 s, which is 6 LED toggles
#define HOLD_TOGGLES	6U

// 2 kHz is near the speaker's 1.7 kHz resonance, so it is easy to hear.
// 32 points x 2000 Hz = 64000 DAC updates a second, one every 1125 CPU cycles
#define TONE_HZ				2000U
#define TONE_POINTS			32U
#define TONE_UPDATE_CYCLES	(SYSCLK_HZ / (TONE_HZ * TONE_POINTS))
#define TONE_UPDATES_PER_TOGGLE	((TONE_HZ * TONE_POINTS) / 2U)

// One cycle of a sine: 2048 + 1000 x sin(2 x pi x i / 32). It stays well inside
// the range the DAC's buffer can reach
static const uint16_t tone_table[TONE_POINTS] = {
	2048, 2243, 2431, 2604, 2755, 2879, 2972, 3029,
	3048, 3029, 2972, 2879, 2755, 2604, 2431, 2243,
	2048, 1853, 1665, 1492, 1341, 1217, 1124, 1067,
	1048, 1067, 1124, 1217, 1341, 1492, 1665, 1853
};

static uint32_t last_tick;

// Right-aligned and always 10 characters wide, so a shorter number fully covers
// the longer one before it
static void lcd_put_value(unsigned short y, const char *label, uint32_t value, uint32_t base) {
	char text[11];	// 10 decimal digits of a uint32_t, plus the terminator
	int i = sizeof(text) - 1;

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

static uint32_t code_to_mv(uint32_t code) {
	return (code * ADC_VREF_MV) / ADC_MAX_CODE;
}

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
		min = ADC_MAX_CODE;
		max = 0U;
		sum = 0U;
		count = 0U;

		// Under 2 million conversions a second, so sum cannot overflow in 0.5 s
		while ((dbg_cycles() - last_tick) < HALF_SECOND_CYCLES) {
			start = dbg_cycles();
			dbg_scope_high();		// PF8 is high for one whole conversion
			adc_start();
			while (!adc_is_done()) {
			}
			dbg_scope_low();
			conv_cycles = dbg_cycles() - start;

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

static void wait_cycles(uint32_t cycles) {
	while ((dbg_cycles() - last_tick) < cycles) {
	}
	last_tick += cycles;	// no drift, unlike re-reading the counter
}

// Holds a fixed voltage for 3 s
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

// Plays the 2 kHz sine for 3 s. PF8 is a 2 kHz square wave in step with it,
// which gives the scope something clean to trigger on
static void dac_play_tone(void) {
	uint32_t phase = 0U;
	uint32_t toggles;
	uint32_t updates;

	lcd_putString(10, 50, (unsigned char *)"OUTPUT: 2 KHZ TONE   ");
	lcd_put_value(65, "TARGET (mV)", 0U, 10U);
	lcd_put_value(80, "DAC CODE", 0U, 10U);

	// The LCD writes above took time, so restart the timing here. Otherwise
	// wait_cycles would rush through the first few updates to catch up
	last_tick = dbg_cycles();

	for (toggles = 0U; toggles < HOLD_TOGGLES; toggles++) {
		for (updates = 0U; updates < TONE_UPDATES_PER_TOGGLE; updates++) {
			wait_cycles(TONE_UPDATE_CYCLES);
			dac_write(tone_table[phase]);

			if (phase == 0U) {
				dbg_scope_high();
			} else if (phase == (TONE_POINTS / 2U)) {
				dbg_scope_low();
			}
			phase = (phase + 1U) % TONE_POINTS;
		}
		//dbg_led_toggle();
	}
}

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
