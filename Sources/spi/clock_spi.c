#include "../../Headers/stm32f103xe.h"
#include "clock_spi.h"

// SPI1 as master, mode 0, 8 bits, MSB first, software chip select, 72 MHz / 256 =
// 281 kHz. Slow on purpose: breadboard wiring is forgiving at that speed
#define CLOCK_SPI_CR1	(SPI_CR1_MSTR | SPI_CR1_BR_2 | SPI_CR1_BR_1 | SPI_CR1_BR_0 | SPI_CR1_SSM | SPI_CR1_SSI)

// Chip selects on port C: PC0 EEPROM, PC1 clock board (J901), PC5 touch
#define CS_EEPROM_PIN	0U
#define CS_CLOCK_PIN	1U
#define CS_TOUCH_PIN	5U

static int initialised = 0;

// Puts 4-bit pin settings into a GPIO control register, leaving other pins alone
static uint32_t set_pin_mode(uint32_t reg, uint32_t pin, uint32_t mode) {
	return (reg & ~(0xFU << (pin * 4U))) | (mode << (pin * 4U));
}

void clock_spi_init(void) {
	RCC->APB2ENR |= RCC_APB2ENR_IOPAEN | RCC_APB2ENR_IOPCEN | RCC_APB2ENR_SPI1EN;

	// Every chip select high before it becomes an output, so no device sees our
	// bytes as its own. touch.c sets PC0 and PC5 the same way later on
	GPIOC->BSRR = (1U << CS_EEPROM_PIN) | (1U << CS_CLOCK_PIN) | (1U << CS_TOUCH_PIN);

	// The three chip selects: push-pull output, 2 MHz (0x2)
	GPIOC->CRL = set_pin_mode(GPIOC->CRL, CS_EEPROM_PIN, 0x2U);
	GPIOC->CRL = set_pin_mode(GPIOC->CRL, CS_CLOCK_PIN, 0x2U);
	GPIOC->CRL = set_pin_mode(GPIOC->CRL, CS_TOUCH_PIN, 0x2U);

	// SPI1 pins, same as touch.c: PA5 SCK and PA7 MOSI as alternate-function
	// push-pull (0xB), PA6 MISO as floating input (0x4). PA3 and PA4 stay analog
	GPIOA->CRL = set_pin_mode(GPIOA->CRL, 5U, 0xBU);
	GPIOA->CRL = set_pin_mode(GPIOA->CRL, 6U, 0x4U);
	GPIOA->CRL = set_pin_mode(GPIOA->CRL, 7U, 0xBU);

	initialised = 1;
}

void clock_spi_send(uint8_t cmd) {
	uint32_t saved_cr1;

	if (!initialised) {
		clock_spi_init();
	}

	// Swap in our settings. The SPI has to be off while they change. Whatever was
	// there before (the touch driver's settings) goes back at the end
	saved_cr1 = SPI1->CR1;
	if (saved_cr1 != (CLOCK_SPI_CR1 | SPI_CR1_SPE)) {
		SPI1->CR1 = 0U;
		SPI1->CR1 = CLOCK_SPI_CR1;
		SPI1->CR1 = CLOCK_SPI_CR1 | SPI_CR1_SPE;
	}

	// Select the clock board, then send the byte
	GPIOC->BSRR = 1U << (CS_CLOCK_PIN + 16U);
	while ((SPI1->SR & SPI_SR_TXE) == 0U) {
	}
	SPI1->DR = cmd;

	// Wait for all 8 bits to go out. Reading DR clears the byte that came back,
	// which is nothing useful because the clock board can't talk back
	while ((SPI1->SR & SPI_SR_RXNE) == 0U) {
	}
	(void)SPI1->DR;
	while (SPI1->SR & SPI_SR_BSY) {
	}

	// Deselect. This rising edge is when the clock board can latch the byte
	GPIOC->BSRR = 1U << CS_CLOCK_PIN;

	// Put the previous settings back
	if (saved_cr1 != (CLOCK_SPI_CR1 | SPI_CR1_SPE)) {
		SPI1->CR1 = 0U;
		SPI1->CR1 = saved_cr1 & ~SPI_CR1_SPE;
		SPI1->CR1 = saved_cr1;
	}
}
