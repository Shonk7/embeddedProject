// ===========================================================================
// main.c: where our program starts, right after SystemInit() sets the clocks
// ===========================================================================
//
// How the whole firmware fits together, for anyone new to the code:
//
//   Clock board (our hardware) ---square wave---> PF11
//        ^                                          |
//        | SPI command bytes (D1/D2/D4/D8/E0/E1)    | every rising edge fires
//        |                                          v the audio interrupt
//   rate_ctrl.c + spi/clock_spi.c          audio.c: EXTI15_10_IRQHandler
//        ^                                    reads the ADC (jack, PA3)
//        | button presses                     writes the DAC (speaker, PA4)
//        |                                    runs the mixer at fx/8 (Sampling/)
//   ui/ui.c: touchscreen pages               feeds the FFT capture (capture.c)
//   (main, mixer, spectrum)
//
// There are only two "threads" in the firmware:
//   1. The audio interrupt. It runs once per clock edge and does all the
//      sampling, as the spec requires. It has to be quick: at fx there are only
//      180 CPU cycles between edges.
//   2. The main loop below. It runs the touchscreen, drawing, SPI commands and
//      the FFT maths. It can be as slow as it likes, because the interrupt
//      simply cuts in whenever an edge arrives. That's why redrawing the screen
//      never drops a sample.
//
// Anything both of them touch is marked volatile and is either a single 32-bit
// value (which can't be half-written) or is handed over with a flag.

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

	// Bring up the full system. The order matters:
	//   1. audio_init: ADC, then DAC, then the PF11 interrupt last, so the first
	//      edge finds everything ready. From here on samples flow by themselves
	//      whenever the clock board is running (plain passthrough)
	//   2. sampling_init: plugs the mixer path into the interrupt. It only does any
	//      work at fx/8; at the other rates it hands samples straight through
	//   3. rate_init: puts the clock board in a known state over SPI (off, fx/8,
	//      on) and lights the LEDs: PF0 fx, PF1 fx/2, PF2 fx/4, PF3 fx/8, PF4 running
	//   4. ui_init: touchscreen, mixer settings, and draws the main page
	audio_init();
	sampling_init();
	rate_init();
	ui_init();

	// The main loop only runs the UI: touch, buttons, live stats and, on the
	// spectrum page, the FFT. Audio carries on underneath it in the interrupt
	while (1) {
		ui_poll();
	}
}
