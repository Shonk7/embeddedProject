#ifndef UI_H
#define UI_H

#include <stdint.h>

// Call after lcd_init() and rate_init(). Draws the main page
void ui_init(void);

// Call every pass of the main loop. Polls the temporary push-button controls
// and the debugger touch hook. Never call from an interrupt: drawing is slow
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
