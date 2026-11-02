#include "../Headers/stm32f103xe.h"
#include "../LCD/ili9341.h"
#include "../LCD/lcd_hw.h"
#include "../LCD/lcd_grph.h"
#include "clock.h"
#include "debug.h"

// Stage 1 clock test. These are counted in CPU cycles on the assumption that the
// CPU really runs at SYSCLK_HZ, so a wrong clock shows up as a wrong frequency:
// 1 kHz on the scope pin and a 1 Hz LED mean exactly 72 MHz
#define HALF_PERIOD_CYCLES	(SYSCLK_HZ / 2000U)		// 0.5 ms
#define TOGGLES_PER_LED		1000U					// 0.5 s

static void lcd_put_value(unsigned short y, const char *label, uint32_t value, uint32_t base) {
	char text[11];	// 10 decimal digits of a uint32_t, plus the terminator
	int i = sizeof(text) - 1;

	text[i] = '\0';
	do {
		text[--i] = "0123456789ABCDEF"[value % base];
		value /= base;
	} while (value != 0U);

	lcd_putString(10, y, (unsigned char *)label);
	lcd_putString(110, y, (unsigned char *)&text[i]);
}

int main(void) {
	uint32_t last;
	uint32_t toggles = 0U;

	// SystemInit cannot leave results in globals, so read them back from hardware
	SystemCoreClockUpdate();
	dbg_init();

	lcd_init();
	lcd_fillScreen(BLACK);
	lcd_fontColor(WHITE, BLACK);

	lcd_putString(10, 10, (unsigned char *)"ELEC4123 STAGE 1 CLOCK TEST");
	lcd_put_value(30, "SYSCLK (Hz)", SystemCoreClock, 10U);
	lcd_put_value(45, "RCC_CFGR (hex)", RCC->CFGR, 16U);
	lcd_put_value(60, "FLASH_ACR (hex)", FLASH->ACR, 16U);

	if (SystemCoreClock != SYSCLK_HZ) {
		dbg_fault_led_on();
		lcd_fontColor(RED, BLACK);
		lcd_putString(10, 80, (unsigned char *)"CLOCK FAULT: NOT ON THE PLL");
	}

	// main must never return. With the standard library that ends in a
	// semihosting BKPT, which faults when no debugger is attached
	last = dbg_cycles();
	for (;;) {
		while ((dbg_cycles() - last) < HALF_PERIOD_CYCLES) {
		}
		last += HALF_PERIOD_CYCLES;		// no drift, unlike re-reading the counter
		dbg_scope_toggle();

		if (++toggles == TOGGLES_PER_LED) {
			toggles = 0U;
			dbg_led_toggle();
		}
	}
}
