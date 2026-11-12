#ifndef UI_H
#define UI_H

#include <stdint.h>

// ui.h: the touchscreen user interface (all of it lives in ui/ui.c).
//
// Three pages:
//   MAIN      pick the rate, start/stop the clock, send the invalid command,
//             and open the other two pages
//   MIXER     Ext A: three gain bars, crossover -/+ buttons, live ISR load
//   SPECTRUM  Ext B.1: 128-point FFT bars, 0 to 20 kHz
// main.c only ever calls ui_init() once and ui_poll() forever.

// Call after lcd_init() and rate_init(). Draws the main page
void ui_init(void);

// Call every pass of the main loop. Checks the touchscreen, the PB0/PB1 backup
// buttons and the debugger touch hook, updates the live stats, and runs the
// spectrum page's frames. Never call from an interrupt: drawing is slow
void ui_poll(void);

// The touch driver calls this once per new press, in screen pixels
// (x 0-239 left to right, y 0-319 top to bottom, portrait)
void ui_touch(uint16_t x, uint16_t y);

// Debugger hook to fake a touch before the touch driver exists: in a debug
// session, write ui_test_y, then ui_test_x. ui_poll() handles it and puts
// ui_test_x back to 0xFFFF
extern volatile uint16_t ui_test_x;
extern volatile uint16_t ui_test_y;

#endif
