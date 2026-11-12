#include "../../Headers/stm32f103xe.h"
#include "../../LCD/lcd_grph.h"
#include "../clock.h"
#include "../debug.h"
#include "touch.h"

// touch.c: reads the resistive touch controller over SPI1 (see touch.h).
//
// A press goes through three stages:
//   touch_poll()      every 10 ms: is the pen down, and has it been down long
//                     enough to be a real press rather than a bounce?
//   touch_read_raw()  8 X and 8 Y readings, rejected if they disagree too much
//   touch_map()       raw 0-4095 numbers -> screen pixels, using touch.h's
//                     calibration

// Control byte: start bit, channel, 12-bit, differential, PD = 00 (pen IRQ on
// between conversions). X+ driven = 0xD0, Y+ driven = 0x90
#define CMD_READ_X		0xD0U
#define CMD_READ_Y		0x90U

#define SAMPLES			8U		// per axis, per reading
#define MAX_SPREAD		100U	// raw counts: noisier than this = reject
#define RAW_MIN_VALID	50U		// 0 or 4095 usually means no contact / wrong CS
#define RAW_MAX_VALID	4045U

#define POLL_CYCLES		(SYSCLK_HZ / 100U)	// check the pen every 10 ms
#define DOWN_POLLS		2U					// 20 ms down before reading
#define UP_POLLS		3U					// 30 ms up before the next press

// SPI1 master, mode 0, 8-bit, MSB first, software NSS, 72 MHz / 64 = 1.125 MHz
// (controller limit is about 2 MHz)
#define TOUCH_SPI_CR1	(SPI_CR1_MSTR | SPI_CR1_BR_2 | SPI_CR1_BR_0 | SPI_CR1_SSM | SPI_CR1_SSI)

// Last good raw reading (see touch.h), for calibrating in the Keil Watch window
volatile uint16_t touch_raw_x = 0U;
volatile uint16_t touch_raw_y = 0U;

// ---------------------------------------------------------------------------
// SPI1 helpers. SPI1 is shared with the clock board and EEPROM, and everyone
// uses it from the main loop only. The touch driver puts its own settings in,
// and puts back whatever was there before when it finishes
// ---------------------------------------------------------------------------
// Whatever SPI1 settings were there before spi_take(), put back by spi_release()
static uint32_t spi_saved_cr1;

// Switch SPI1 to the touch controller's speed and mode
static void spi_take(void) {
	spi_saved_cr1 = SPI1->CR1;
	if ((spi_saved_cr1 & ~SPI_CR1_SPE) != TOUCH_SPI_CR1) {
		SPI1->CR1 = 0U;						// SPE off before changing settings
		SPI1->CR1 = TOUCH_SPI_CR1;
	}
	SPI1->CR1 = TOUCH_SPI_CR1 | SPI_CR1_SPE;
}

// Wait for the last bit to finish, then give SPI1 back as we found it
static void spi_release(void) {
	while (SPI1->SR & SPI_SR_BSY) {
	}
	if (spi_saved_cr1 != (TOUCH_SPI_CR1 | SPI_CR1_SPE)) {
		SPI1->CR1 = 0U;
		SPI1->CR1 = spi_saved_cr1 & ~SPI_CR1_SPE;
		SPI1->CR1 = spi_saved_cr1;
	}
}

// Sends one byte and returns the byte that came back at the same time. SPI
// always swaps a byte for a byte, so reading means sending dummy zeros
static uint8_t spi_xfer(uint8_t out) {
	while ((SPI1->SR & SPI_SR_TXE) == 0U) {
	}
	SPI1->DR = out;
	while ((SPI1->SR & SPI_SR_RXNE) == 0U) {
	}
	return (uint8_t)SPI1->DR;
}

// Chip select is active low: low = "I'm talking to you, touch controller"
static void cs_low(void) {
	TOUCH_CS_PORT->BSRR = 1U << (TOUCH_CS_PIN + 16U);
}

static void cs_high(void) {
	TOUCH_CS_PORT->BSRR = 1U << TOUCH_CS_PIN;
}

// The 12-bit result arrives in the 16 clocks after the command, MSB first,
// followed by 3 zero bits: bits 14..3 of the two received bytes
static uint16_t read_channel(uint8_t cmd) {
	uint16_t hi, lo;

	(void)spi_xfer(cmd);
	hi = spi_xfer(0x00U);
	lo = spi_xfer(0x00U);
	return (uint16_t)((((hi << 8) | lo) >> 3) & 0x0FFFU);
}

// Average of the samples with the highest and lowest dropped. Returns 0 (reject)
// if the readings spread too far, or sit at the very ends of the range, which is
// what a half-press or a sliding finger looks like
static int filter(const uint16_t *s, uint16_t *out) {
	uint32_t i, sum = 0U;
	uint16_t lo = 0xFFFFU, hi = 0U;

	for (i = 0U; i < SAMPLES; i++) {
		sum += s[i];
		if (s[i] < lo) { lo = s[i]; }
		if (s[i] > hi) { hi = s[i]; }
	}
	if (((hi - lo) > MAX_SPREAD) || (lo < RAW_MIN_VALID) || (hi > RAW_MAX_VALID)) {
		return 0;
	}
	*out = (uint16_t)((sum - lo - hi) / (SAMPLES - 2U));
	return 1;
}

// ---------------------------------------------------------------------------
// Pin setup. Called once from ui_init()
void touch_init(void) {
	// Clocks for port A (SPI pins, pen-down), port C (chip selects) and SPI1
	RCC->APB2ENR |=RCC_APB2ENR_IOPAEN | RCC_APB2ENR_IOPCEN | TOUCH_CS_PORT_EN
	              | RCC_APB2ENR_SPI1EN;

	// Chip selects high (idle) before they become outputs. PC0 is the EEPROM:
	// it must stay deselected or it could drive MISO during touch reads
	cs_high();
	GPIOC->BSRR = 1U << 0;

	// Touch CS and PC0: push-pull output, 2 MHz (0x2)
	if (TOUCH_CS_PIN < 8U) {
		TOUCH_CS_PORT->CRL = (TOUCH_CS_PORT->CRL & ~(0xFU << (TOUCH_CS_PIN * 4U)))
		                   | (0x2U << (TOUCH_CS_PIN * 4U));
	}
	GPIOC->CRL = (GPIOC->CRL & ~0xFU) | 0x2U;

	// PA0 (PENIRQ, open drain, low when pressed): input with pull-up
	// (CNF = 10, MODE = 00, ODR = 1). PA3 (ADC) and PA4 (DAC) are not touched
	GPIOA->BSRR = 1U << 0;
	GPIOA->CRL = (GPIOA->CRL & ~0xFU) | 0x8U;

	// SPI1 pins: PA5 SCK and PA7 MOSI alternate-function push-pull 50 MHz (0xB),
	// PA6 MISO floating input (0x4)
	GPIOA->CRL = (GPIOA->CRL & ~((0xFU << 20) | (0xFU << 24) | (0xFU << 28)))
	           | (0xBU << 20) | (0x4U << 24) | (0xBU << 28);

	// One dummy conversion leaves the controller in PD = 00 (pen IRQ enabled)
	spi_take();
	cs_low();
	(void)read_channel(CMD_READ_X);
	cs_high();
	spi_release();
}

// The controller pulls PA0 low while the screen is pressed
int touch_is_down(void) {
	return (GPIOA->IDR & 0x1U) == 0U;
}

// Takes 8 X/Y pairs in one go, filters each axis, and only accepts the result if
// the pen is still down at the end (a lift mid-reading gives garbage)
int touch_read_raw(uint16_t *raw_x, uint16_t *raw_y) {
	uint16_t sx[SAMPLES], sy[SAMPLES];
	uint32_t i;
	int ok;

	spi_take();
	cs_low();
	for (i = 0U; i < SAMPLES; i++) {
		sx[i] = read_channel(CMD_READ_X);
		sy[i] = read_channel(CMD_READ_Y);
	}
	cs_high();
	spi_release();

	ok = filter(sx, raw_x) && filter(sy, raw_y) && touch_is_down();
	if (ok) {
		touch_raw_x = *raw_x;
		touch_raw_y = *raw_y;
	}
	return ok;
}

// Linear map of one axis: raw a -> pixel 0, raw b -> pixel (size - 1).
// Signed maths, so a > b (a reversed axis) works too
static uint16_t map_axis(int32_t raw, int32_t a, int32_t b, int32_t size) {
	int32_t p = ((raw - a) * (size - 1)) / (b - a);

	if (p < 0) { p = 0; }
	if (p > size - 1) { p = size - 1; }
	return (uint16_t)p;
}

// Raw reading to screen pixels, swapping the axes first if the panel is wired
// that way round
void touch_map(uint16_t raw_x, uint16_t raw_y, uint16_t *x, uint16_t *y) {
#if TOUCH_SWAP_XY
	uint16_t t = raw_x;
	raw_x = raw_y;
	raw_y = t;
#endif
	*x = map_axis(raw_x, TOUCH_RAW_AT_LEFT, TOUCH_RAW_AT_RIGHT, DISPLAY_WIDTH);
	*y = map_axis(raw_y, TOUCH_RAW_AT_TOP, TOUCH_RAW_AT_BOTTOM, DISPLAY_HEIGHT);
}

// ---------------------------------------------------------------------------
// Press detection: pen must be down for 20 ms, then one good reading gives one
// press event. The pen must then be up for 30 ms before the next press counts
// ---------------------------------------------------------------------------
static uint32_t last_poll;		// cycle count of the last 10 ms check
static uint32_t down_count;		// checks in a row with the pen down
static uint32_t up_count;		// checks in a row with the pen up
static int pressed;				// 1 = this press has already been reported

// Called every pass of the main loop, but only does anything every 10 ms. Never
// waits, so the UI stays responsive
int touch_poll(uint16_t *x, uint16_t *y) {
	uint16_t rx, ry;

	if ((dbg_cycles() - last_poll) < POLL_CYCLES) {
		return 0;
	}
	last_poll = dbg_cycles();

	// Waiting for a new press: report it once the pen has been down 20 ms
	if (!pressed) {
		if (!touch_is_down()) {
			down_count = 0U;
			return 0;
		}
		if (++down_count < DOWN_POLLS) {
			return 0;
		}
		if (!touch_read_raw(&rx, &ry)) {
			return 0;			// noisy: try again next poll
		}
		pressed = 1;
		up_count = 0U;
		touch_map(rx, ry, x, y);
		return 1;
	}

	// Already reported: wait for the pen to stay up 30 ms before arming again,
	// so holding a finger on a button counts as one tap, not many
	if (touch_is_down()) {
		up_count = 0U;
	} else if (++up_count >= UP_POLLS) {
		pressed = 0;
		down_count = 0U;
	}
	return 0;
}
