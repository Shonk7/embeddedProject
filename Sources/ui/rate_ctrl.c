#include "../../Headers/stm32f103xe.h"
#include "rate_ctrl.h"

// SPI note: when the SPI module exists, include its header here, e.g.
// #include "../<spi folder>/<spi header>.h"
// and uncomment the two clock_spi_send() lines below (in rate_set and rate_enable)

// Indicator LEDs on port F, active high (LED1-LED5 on the board).
// PF0, PF7 and PF8 belong to debug.c, so they are left alone here
#define LED_RATE_FIRST	0U								// PF1 fx, PF2 fx/2, PF3 fx/4, PF4 fx/8
#define LED_RATE_MASK	(0xFU << LED_RATE_FIRST)
#define LED_ENABLE_BIT	(1U << 4)						// PF5 on = clock enabled
#define LED_ALL_MASK	(LED_RATE_MASK | LED_ENABLE_BIT)

static const uint8_t rate_cmd[RATE_COUNT] = {
	CMD_RATE_FX, CMD_RATE_FX_2, CMD_RATE_FX_4, CMD_RATE_FX_8
};

static const char * const rate_text[RATE_COUNT] = {
	"399.25 kHz", "199.63 kHz", "99.81 kHz", "49.91 kHz"
};

static const char * const rate_text_short[RATE_COUNT] = {
	"399 kHz", "200 kHz", "100 kHz", "50 kHz"
};

static rate_t current_rate = RATE_FX_8;
static int clock_enabled = 0;

volatile uint8_t clock_last_cmd = 0U;

// One BSRR write: the selected rate's LED on, the other three off, and the
// enable LED following the state. Set and reset bits never overlap
static void leds_update(void) {
	uint32_t on = 1U << (LED_RATE_FIRST + (uint32_t)current_rate);
	uint32_t off = LED_RATE_MASK & ~on;

	if (clock_enabled) {
		on |= LED_ENABLE_BIT;
	} else {
		off |= LED_ENABLE_BIT;
	}
	GPIOF->BSRR = on | (off << 16);
}

void rate_init(void) {
	uint32_t pin;

	RCC->APB2ENR |= RCC_APB2ENR_IOPFEN;

	// Levels first so the pins don't flash when they become outputs
	GPIOF->BSRR = LED_ALL_MASK << 16;

	// PF1-PF5: push-pull output, 2 MHz (CNF = 00, MODE = 10). 4 bits per pin in CRL
	for (pin = 0U; pin <= 4U; pin++) {
		GPIOF->CRL = (GPIOF->CRL & ~(0xFU << (pin * 4U))) | (0x2U << (pin * 4U));
	}

	// Known start state. Disable first so no partial rate is ever presented
	rate_enable(0);
	rate_set(RATE_FX_8);
	rate_enable(1);
}

void rate_set(rate_t rate) {
	if ((uint32_t)rate >= (uint32_t)RATE_COUNT) {
		return;
	}
	clock_last_cmd = rate_cmd[rate];
	// clock_spi_send(rate_cmd[rate]);		// SPI: uncomment when the SPI module exists
	current_rate = rate;
	leds_update();
}

void rate_enable(int on) {
	clock_last_cmd = on ? CMD_ENABLE : CMD_DISABLE;
	// clock_spi_send(on ? CMD_ENABLE : CMD_DISABLE);	// SPI: uncomment when the SPI module exists
	clock_enabled = (on != 0);
	leds_update();
}

rate_t rate_get(void) {
	return current_rate;
}

int rate_is_enabled(void) {
	return clock_enabled;
}

uint32_t rate_hz(rate_t rate) {
	return FX_HZ >> (uint32_t)rate;
}

const char *rate_label(rate_t rate) {
	return rate_text[rate];
}

const char *rate_short_label(rate_t rate) {
	return rate_text_short[rate];
}
