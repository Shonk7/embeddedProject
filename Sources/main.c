#include "../Headers/stm32f103xe.h"
#include "../LCD/ili9341.h"
#include "../LCD/lcd_hw.h"
#include "../LCD/lcd_grph.h"
#include "clock.h"
#include "debug.h"
#include "tests.h"

// Which bench test to run: 2 = ADC, 3 = DAC, 4 = external clock
#define ACTIVE_TEST		4

int main(void) {
	// SystemInit can't leave results in globals, so read the speed back from hardware
	SystemCoreClockUpdate();
	dbg_init();

	// Start the screen with a black background
	lcd_init();
	lcd_fillScreen(BLACK);
	lcd_fontColor(WHITE, BLACK);

	// Warn on screen and light the red LED if we didn't reach 72 MHz
	if (SystemCoreClock != SYSCLK_HZ) {
		dbg_fault_led_on();
		lcd_fontColor(RED, BLACK);
		lcd_putString(10, 185, (unsigned char *)"CLOCK FAULT: NOT ON THE PLL");
		lcd_fontColor(WHITE, BLACK);
	}

	// Run the chosen test. It never returns, which matters because leaving main
	// ends in a breakpoint that faults when no debugger is attached
#if ACTIVE_TEST == 2
	test_adc();
#elif ACTIVE_TEST == 3
	test_dac();
#else
	test_extclk();
#endif

	for (;;) {
	}
}
