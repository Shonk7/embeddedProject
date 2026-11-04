#include "../Headers/stm32f103xe.h"
#include "../LCD/ili9341.h"
#include "../LCD/lcd_hw.h"
#include "../LCD/lcd_grph.h"
#include "clock.h"
#include "debug.h"
#include "tests.h"
#include "ui/rate_ctrl.h"
#include "ui/ui.h"
#include "ui/touch.h"

// Which bench test to run: 2 = ADC, 3 = DAC, 4 = main page UI, 5 = touch calibration
#define ACTIVE_TEST		4
// Test comment

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
#elif ACTIVE_TEST == 3
	test_dac();
#elif ACTIVE_TEST == 5
	touch_calibration_screen();
#else
	// Main page. LEDs: PF1 fx, PF2 fx/2, PF3 fx/4, PF4 fx/8, PF5 clock enabled
	rate_init();
	ui_init();
#endif

	for (;;) {
		ui_poll();
	}
}
