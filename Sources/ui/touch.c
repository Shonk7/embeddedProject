#include "../../Headers/stm32f103xe.h"
#include "../../LCD/lcd_grph.h"
#include "../clock.h"
#include "../debug.h"
#include "touch.h"

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

volatile uint16_t touch_raw_x = 0U;
volatile uint16_t touch_raw_y = 0U;

// ---------------------------------------------------------------------------
// SPI1 helpers. SPI1 is shared with the clock board and EEPROM, and everyone
// uses it from the main loop only. The touch driver puts its own settings in,
// and puts back whatever was there before when it finishes
// ---------------------------------------------------------------------------
static uint32_t spi_saved_cr1;

static void spi_take(void) {
	spi_saved_cr1 = SPI1->CR1;
	if ((spi_saved_cr1 & ~SPI_CR1_SPE) != TOUCH_SPI_CR1) {
		SPI1->CR1 = 0U;						// SPE off before changing settings
		SPI1->CR1 = TOUCH_SPI_CR1;
	}
	SPI1->CR1 = TOUCH_SPI_CR1 | SPI_CR1_SPE;
}

static void spi_release(void) {
	while (SPI1->SR & SPI_SR_BSY) {
	}
	if (spi_saved_cr1 != (TOUCH_SPI_CR1 | SPI_CR1_SPE)) {
		SPI1->CR1 = 0U;
		SPI1->CR1 = spi_saved_cr1 & ~SPI_CR1_SPE;
		SPI1->CR1 = spi_saved_cr1;
	}
}

static uint8_t spi_xfer(uint8_t out) {
	while ((SPI1->SR & SPI_SR_TXE) == 0U) {
	}
	SPI1->DR = out;
	while ((SPI1->SR & SPI_SR_RXNE) == 0U) {
	}
	return (uint8_t)SPI1->DR;
}

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

// Average of the samples with the highest and lowest dropped
static bool filter(const uint16_t *s, uint16_t *out) {
	uint32_t i, sum = 0U;
	uint16_t lo = 0xFFFFU, hi = 0U;

	for (i = 0U; i < SAMPLES; i++) {
		sum += s[i];
		if (s[i] < lo) { lo = s[i]; }
		if (s[i] > hi) { hi = s[i]; }
	}
	if (((hi - lo) > MAX_SPREAD) || (lo < RAW_MIN_VALID) || (hi > RAW_MAX_VALID)) {
		return false;
	}
	*out = (uint16_t)((sum - lo - hi) / (SAMPLES - 2U));
	return true;
}

// ---------------------------------------------------------------------------
void touch_init(void) {
	RCC->APB2ENR |= RCC_APB2ENR_IOPAEN | RCC_APB2ENR_IOPCEN | TOUCH_CS_PORT_EN
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

bool touch_is_down(void) {
	return (GPIOA->IDR & 0x1U) == 0U;
}

bool touch_read_raw(uint16_t *raw_x, uint16_t *raw_y) {
	uint16_t sx[SAMPLES], sy[SAMPLES];
	uint32_t i;
	bool ok;

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
static uint32_t last_poll;
static uint32_t down_count;
static uint32_t up_count;
static int pressed;

bool touch_poll(uint16_t *x, uint16_t *y) {
	uint16_t rx, ry;

	if ((dbg_cycles() - last_poll) < POLL_CYCLES) {
		return false;
	}
	last_poll = dbg_cycles();

	if (!pressed) {
		if (!touch_is_down()) {
			down_count = 0U;
			return false;
		}
		if (++down_count < DOWN_POLLS) {
			return false;
		}
		if (!touch_read_raw(&rx, &ry)) {
			return false;			// noisy: try again next poll
		}
		pressed = 1;
		up_count = 0U;
		touch_map(rx, ry, x, y);
		return true;
	}

	if (touch_is_down()) {
		up_count = 0U;
	} else if (++up_count >= UP_POLLS) {
		pressed = 0;
		down_count = 0U;
	}
	return false;
}
