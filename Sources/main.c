#include "../Headers/stm32f103xe.h"
#include "../LCD/ili9341.h"
#include "../LCD/lcd_hw.h"
#include "../LCD/lcd_grph.h"
#include "clock.h"
#include "debug.h"
#include "tests.h"
#include "audio.h"
#include "ui/rate_ctrl.h"
#include "ui/ui.h"
#include "ui/touch.h"

// What to run:
//   4 = the full system: touchscreen, audio passthrough and clock board control
//   2 = ADC test, 3 = DAC test, 5 = touch calibration, 6 = audio stats,
//   7 = external clock count, 8 = SPI command test,
//   9 = SPI loopback (no clock board, jumper J901 pin 5 to pin 3)
#define ACTIVE_TEST		4
// Test comment

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

	// Run the chosen test. Tests never return, which matters because leaving main
	// ends in a breakpoint that faults when no debugger is attached
#if ACTIVE_TEST == 2
	test_adc();
#elif ACTIVE_TEST == 3
	test_dac();
#elif ACTIVE_TEST == 5
	touch_calibration_screen();
#elif ACTIVE_TEST == 6
	test_audio();
#elif ACTIVE_TEST == 7
	test_extclk();
#elif ACTIVE_TEST == 8
	test_spi();
#elif ACTIVE_TEST == 9
	test_spi_loopback();
#else
	// Full system. Audio starts first and then runs by itself in the ISR. rate_init
	// puts the clock board in a known state over SPI (off, fx/8, on), then the UI
	// takes over. LEDs: PF0 fx, PF1 fx/2, PF2 fx/4, PF3 fx/8, PF4 clock running
	audio_init();
	rate_init();
	ui_init();
#endif

	// The touchscreen runs here. Audio carries on underneath it in the ISR
	while (1) {
		ui_poll();
	}
}
