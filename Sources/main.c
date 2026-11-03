#include "../Headers/stm32f103xe.h"
#include "../LCD/ili9341.h"
#include "../LCD/lcd_hw.h"
#include "../LCD/lcd_grph.h"
#include "clock.h"
#include "debug.h"
#include "tests.h"

// Which bench test to run: 2 = ADC, 3 = DAC
#define ACTIVE_TEST		3

int main(void) {
	// SystemInit cannot leave results in globals, so read them back from hardware
	SystemCoreClockUpdate();
	dbg_init();

	lcd_init();
	lcd_fillScreen(BLACK);
	lcd_fontColor(WHITE, BLACK);

	if (SystemCoreClock != SYSCLK_HZ) {
		dbg_fault_led_on();
		lcd_fontColor(RED, BLACK);
		lcd_putString(10, 185, (unsigned char *)"CLOCK FAULT: NOT ON THE PLL");
		lcd_fontColor(WHITE, BLACK);
	}

	// Neither test returns. main must never return: with the standard library
	// that ends in a semihosting BKPT, which faults when no debugger is attached
#if ACTIVE_TEST == 2
	test_adc();
#else
	test_dac();
#endif

	for (;;) {
	}
}
