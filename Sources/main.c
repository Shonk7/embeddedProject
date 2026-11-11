#include "../Headers/stm32f103xe.h"
#include "../LCD/ili9341.h"
#include "../LCD/lcd_hw.h"
#include "../LCD/lcd_grph.h"
#include "clock.h"
#include "debug.h"
#include "audio.h"
#include "ui/rate_ctrl.h"
#include "ui/ui.h"
#include "Sampling/sampling.h"

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

	// Full system. Audio starts first and then runs by itself in the ISR (which also
	// feeds the FFT capture). Sampling plugs the mixer into the ISR, active at fx/8
	// only. rate_init puts the
	// clock board in a known state over SPI (off, fx/8, on), then the UI takes
	// over. LEDs: PF0 fx, PF1 fx/2, PF2 fx/4, PF3 fx/8, PF4 clock running
	audio_init();
	sampling_init();
	rate_init();
	ui_init();

	// The touchscreen runs here. Audio carries on underneath it in the ISR
	while (1) {
		ui_poll();
	}
}
