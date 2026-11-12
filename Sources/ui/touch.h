#ifndef TOUCH_H
#define TOUCH_H

#include <stdint.h>
#include <stdbool.h>

// Resistive touch controller U902 on SPI1 (XPT2046 / ADS7843 / TSC2046 family -
// all use the same commands). Pen-down signal on PA0 (J501 must be on the LEFT)

// ---- Chip select. Team notes say PC5; the board silkscreen may say PG5.
// If the calibration screen shows raw values stuck at 0 or 4095, try PG5:
// set TOUCH_CS_PORT to GPIOG, TOUCH_CS_PIN 5, TOUCH_CS_PORT_EN RCC_APB2ENR_IOPGEN
#define TOUCH_CS_PORT		GPIOC
#define TOUCH_CS_PIN		5U
#define TOUCH_CS_PORT_EN	RCC_APB2ENR_IOPCEN

// ---- Calibration: raw 12-bit reading (0-4095) at each screen edge.
#define TOUCH_SWAP_XY			0		// 1 if moving left-right changes raw Y
#define TOUCH_RAW_AT_LEFT		200
#define TOUCH_RAW_AT_RIGHT		3900
#define TOUCH_RAW_AT_TOP		200
#define TOUCH_RAW_AT_BOTTOM		3900

void touch_init(void);

// true while the panel is being pressed (PENIRQ low)
bool touch_is_down(void);

// Reads raw X and Y (8 samples each, outliers dropped). Returns false if the
// samples were too noisy or the pen lifted, so the result is unusable
bool touch_read_raw(uint16_t *raw_x, uint16_t *raw_y);

// Raw reading -> screen pixels (x 0-239, y 0-319, portrait), using the
// calibration above
void touch_map(uint16_t raw_x, uint16_t raw_y, uint16_t *x, uint16_t *y);

// Main loop only. Returns true exactly once per new press, with its position.
// A press must be released before the next one counts
bool touch_poll(uint16_t *x, uint16_t *y);


// Last raw reading, for the Keil Watch window
extern volatile uint16_t touch_raw_x;
extern volatile uint16_t touch_raw_y;

#endif
