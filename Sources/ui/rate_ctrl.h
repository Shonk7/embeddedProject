#ifndef RATE_CTRL_H
#define RATE_CTRL_H

#include <stdint.h>
#include <stdbool.h>

// fx = 352 kHz + average of the zID endings (08, 67, 72, 42) = 352 + 47.25
#define FX_HZ	399250U

// Index n selects fx / 2^n, matching the clock board's mux inputs I0 to I3
typedef enum {
	RATE_FX = 0,
	RATE_FX_2,
	RATE_FX_4,
	RATE_FX_8,
	RATE_COUNT
} rate_t;

// Clock board command bytes (spec p.4)
#define CMD_RATE_FX		0xD1U
#define CMD_RATE_FX_2	0xD2U
#define CMD_RATE_FX_4	0xD4U
#define CMD_RATE_FX_8	0xD8U
#define CMD_DISABLE		0xE0U
#define CMD_ENABLE		0xE1U

// Deliberately invalid byte for the demo. The clock board must ignore it: no change
// to the rate or enable state, and no glitch (spec p.4)
#define CMD_INVALID_TEST	0xD3U

// Indicator LEDs: sets up PF1-PF5 and puts the clock board in a known state
// (disable, fx/8, enable), because its flip-flops power up random
void rate_init(void);

// Main loop only. Each one produces a command byte and updates the LEDs
void rate_set(rate_t rate);
void rate_enable(int on);

// Sends CMD_INVALID_TEST. Changes nothing on our side (rate, enable state, LEDs),
// because the clock board must ignore it too. Main loop only
void rate_send_invalid(void);

rate_t rate_get(void);
bool rate_is_enabled(void);
uint32_t rate_hz(rate_t rate);
const char *rate_label(rate_t rate);		// "399.25 kHz"
const char *rate_short_label(rate_t rate);	// "399 kHz"

// The last command byte produced (0 = none yet). For the Keil Watch window,
// so the bytes can be checked before the SPI module exists
extern volatile uint8_t clock_last_cmd;

// Sending: rate_set() and rate_enable() call clock_spi_send() from
// Sources/spi/clock_spi.c

#endif
