#include "../Headers/stm32f103xe.h"
#include "../LCD/ili9341.h"
#include "../LCD/lcd_hw.h"
#include "../LCD/lcd_grph.h"
#include "clock.h"
#include "debug.h"
#include "adc.h"

// Stage 2 ADC test. This samples from the main loop only to check the ADC: the
// spec requires the real audio path to sample in the external timer's ISR.
// Counted in CPU cycles, so LED0 still flashes once a second only at 72 MHz
#define REPORT_CYCLES	(SYSCLK_HZ / 2U)	// 0.5 s

#define ADC_MAX_CODE	4095U
#define ADC_VREF_MV		3300U

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

int main(void) {
	uint32_t last;
	uint32_t start;
	uint32_t code;
	uint32_t min;
	uint32_t max;
	uint32_t sum;
	uint32_t count;
	uint32_t conv_cycles = 0U;

	// SystemInit cannot leave results in globals, so read them back from hardware
	SystemCoreClockUpdate();
	dbg_init();

	lcd_init();
	lcd_fillScreen(BLACK);
	lcd_fontColor(WHITE, BLACK);

	adc_init();

	lcd_putString(10, 10, (unsigned char *)"ELEC4123 STAGE 2 ADC TEST");
	lcd_put_value(30, "SYSCLK (Hz)", SystemCoreClock, 10U);

	if (SystemCoreClock != SYSCLK_HZ) {
		dbg_fault_led_on();
		lcd_fontColor(RED, BLACK);
		lcd_putString(10, 185, (unsigned char *)"CLOCK FAULT: NOT ON THE PLL");
		lcd_fontColor(WHITE, BLACK);
	}

	// main must never return. With the standard library that ends in a
	// semihosting BKPT, which faults when no debugger is attached
	last = dbg_cycles();
	for (;;) {
		min = ADC_MAX_CODE;
		max = 0U;
		sum = 0U;
		count = 0U;

		// Under 2 million conversions a second, so sum cannot overflow in 0.5 s
		while ((dbg_cycles() - last) < REPORT_CYCLES) {
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
		last += REPORT_CYCLES;

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
