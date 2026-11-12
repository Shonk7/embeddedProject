#include "../../Headers/stm32f103xe.h"
#include "../clock.h"
#include "../debug.h"
#include "clock_spi.h"

// clock_spi.c: sends one command byte to the clock board as a full transaction
// (shift, latch, apply). See clock_spi.h for the wiring and the timing rules.
//
// SPI1 is shared with the touch controller and the EEPROM, which all hang off the
// same SCLK/MOSI/MISO wires. Each user puts its own speed and mode into SPI1 while
// it talks, then puts back what was there. Everyone uses SPI1 from the main loop
// only, so two users can never be halfway through at the same time.

// SPI1 as master, mode 0, 8 bits, MSB first, software NSS, 72 MHz / 256 =
// 281 kHz. Slow on purpose: breadboard wiring is forgiving at that speed
#define CLOCK_SPI_CR1	(SPI_CR1_MSTR | SPI_CR1_BR_2 | SPI_CR1_BR_1 | SPI_CR1_BR_0 | SPI_CR1_SSM | SPI_CR1_SSI)

// Port C pins
#define CS_EEPROM_PIN	0U		// EEPROM chip select, active low: held HIGH
#define CMD_LATCH_PIN	1U		// clock board CMD_LATCH (J901 pin 6), idle LOW
#define CS_TOUCH_PIN	5U		// touch chip select, active low: held HIGH

// Clock board APPLY (J1101 pin 5), idle LOW. Pin 10 lives in CRH (nibble 10-8)
#define APPLY_PORT		GPIOF
#define APPLY_PIN		10U

// Guard times from the board doc, with margin: 2 us minimums -> 5 us,
// 40 us minimums -> 50 us
#define GUARD_SHORT_US	5U
#define GUARD_LONG_US	50U

// 1 once clock_spi_init() has run. clock_spi_send() sets things up itself the
// first time, so callers don't have to remember to
static int initialised = 0;

// Busy-wait using the CPU cycle counter (72 cycles per us)
static void delay_us(uint32_t us) {
	uint32_t start = dbg_cycles();
	uint32_t wait = us * (SYSCLK_HZ / 1000000U);

	while ((dbg_cycles() - start) < wait) {
	}
}

// Puts 4-bit pin settings into a GPIO control register, leaving other pins alone.
// Each pin gets 4 bits, CNF (2 bits) then MODE (2 bits). The values used here:
//   0x2 = general-purpose push-pull output, 2 MHz (our strobes and chip selects)
//   0xB = alternate-function push-pull, 50 MHz (SPI1 drives the pin, not us)
//   0x4 = floating input (MISO)
static uint32_t set_pin_mode(uint32_t reg, uint32_t pin, uint32_t mode) {
	return (reg & ~(0xFU << (pin * 4U))) | (mode << (pin * 4U));
}

// One-time pin and SPI setup (see clock_spi.h). Called by clock_spi_send() the
// first time if nobody called it earlier
void clock_spi_init(void) {
	// Clocks for ports A (SPI pins), C (latch, chip selects), F (APPLY) and SPI1
	RCC->APB2ENR |=RCC_APB2ENR_IOPAEN | RCC_APB2ENR_IOPCEN | RCC_APB2ENR_IOPFEN | RCC_APB2ENR_SPI1EN;

	// delay_us() needs the cycle counter. dbg_init() normally starts it, but
	// make sure here so this module also works on its own
	CoreDebug->DEMCR |= CoreDebug_DEMCR_TRCENA_Msk;
	DWT->CTRL |= DWT_CTRL_CYCCNTENA_Msk;

	// Levels before the pins become outputs: strobes LOW, other chip selects HIGH
	GPIOC->BSRR = (1U << (CMD_LATCH_PIN + 16U))
	            | (1U << CS_EEPROM_PIN) | (1U << CS_TOUCH_PIN);
	APPLY_PORT->BSRR = 1U << (APPLY_PIN + 16U);

	// All four: push-pull output, 2 MHz (0x2)
	GPIOC->CRL = set_pin_mode(GPIOC->CRL, CS_EEPROM_PIN, 0x2U);
	GPIOC->CRL = set_pin_mode(GPIOC->CRL, CMD_LATCH_PIN, 0x2U);
	GPIOC->CRL = set_pin_mode(GPIOC->CRL, CS_TOUCH_PIN, 0x2U);
	APPLY_PORT->CRH = set_pin_mode(APPLY_PORT->CRH, APPLY_PIN - 8U, 0x2U);

	// SPI1 pins, same as touch.c: PA5 SCLK and PA7 MOSI as alternate-function
	// push-pull (0xB), PA6 MISO as floating input (0x4). PA3 and PA4 stay analog
	GPIOA->CRL = set_pin_mode(GPIOA->CRL, 5U, 0xBU);
	GPIOA->CRL = set_pin_mode(GPIOA->CRL, 6U, 0x4U);
	GPIOA->CRL = set_pin_mode(GPIOA->CRL, 7U, 0xBU);

	// Step 1 guard for the very first command: APPLY has been LOW >= 40 us
	delay_us(GUARD_LONG_US);

	initialised = 1;
}

// Sends one byte as a complete 5-step transaction, about 190 us, then returns.
// The numbered comments below match the steps listed in clock_spi.h
uint8_t clock_spi_send(uint8_t cmd) {
	uint32_t saved_cr1;
	uint8_t echo;

	if (!initialised) {
		clock_spi_init();
	}

	// Swap in our SPI settings (SPE off while they change). The previous
	// settings, e.g. the touch driver's, go back at the end
	saved_cr1 = SPI1->CR1;
	if (saved_cr1 != (CLOCK_SPI_CR1 | SPI_CR1_SPE)) {
		SPI1->CR1 = 0U;
		SPI1->CR1 = CLOCK_SPI_CR1;
		SPI1->CR1 = CLOCK_SPI_CR1 | SPI_CR1_SPE;
	}

	// 1. APPLY and CMD_LATCH are LOW here (idle). The step 5 guard of the
	//    previous send already gave >= 40 us, so the byte can go straight out

	// 2. Shift the byte: 8 SCLK pulses, MSB first. Wait until the last bit has
	//    left (RXNE, then BSY clear), so SCLK has made its final falling edge
	while ((SPI1->SR & SPI_SR_TXE) == 0U) {
	}
	SPI1->DR = cmd;
	while ((SPI1->SR & SPI_SR_RXNE) == 0U) {
	}
	echo = (uint8_t)SPI1->DR;		// clock board sends nothing; loopback test reads it
	while (SPI1->SR & SPI_SR_BSY) {
	}

	// 3. CMD_LATCH pulse: U4 copies the byte to its outputs on the rising edge
	delay_us(GUARD_SHORT_US);
	GPIOC->BSRR = 1U << CMD_LATCH_PIN;					// HIGH
	delay_us(GUARD_SHORT_US);
	GPIOC->BSRR = 1U << (CMD_LATCH_PIN + 16U);			// LOW
	delay_us(GUARD_SHORT_US);							// decoder settles

	// 4. APPLY: the board syncs it to its own clock and, for a valid byte only,
	//    updates the frequency or enable state at a safe moment
	APPLY_PORT->BSRR = 1U << APPLY_PIN;						// HIGH
	delay_us(GUARD_LONG_US);

	// 5. APPLY LOW, then keep the bus quiet until APPLY_SAFE has fallen
	APPLY_PORT->BSRR = 1U << (APPLY_PIN + 16U);				// LOW
	delay_us(GUARD_LONG_US);

	// Put the previous SPI settings back
	if (saved_cr1 != (CLOCK_SPI_CR1 | SPI_CR1_SPE)) {
		SPI1->CR1 = 0U;
		SPI1->CR1 = saved_cr1 & ~SPI_CR1_SPE;
		SPI1->CR1 = saved_cr1;
	}

	return echo;
}
