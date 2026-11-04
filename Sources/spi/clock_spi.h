#ifndef CLOCK_SPI_H
#define CLOCK_SPI_H

#include <stdint.h>

// Sends command bytes to the clock board over SPI1. Its chip select is PC1,
// on J901 pin 6 (pin 4 SCK, pin 5 MOSI, pin 1 GND, pin 2 3.3 V)
//
// Settings the clock board has to match: mode 0 (clock idles low, data read on
// the rising edge), MSB first, 8 bits, 281 kHz. CS goes low before the byte and
// back high after it, so its rising edge can latch the byte

// Sets up PC1 and the SPI1 pins, and holds every other SPI1 chip select high.
// Safe to call more than once
void clock_spi_init(void);

// Sends one byte and waits until it's finished. Any byte goes out, including
// invalid ones, so the demo can show the board ignoring them. Main loop only:
// touch.c shares SPI1, and the two must never run at once.
// Returns the byte that came back on MISO. The clock board never sends anything,
// but with a jumper from MOSI to MISO it's an echo of what we sent
uint8_t clock_spi_send(uint8_t cmd);

// The last byte that came back, for the Keil Watch window
extern volatile uint8_t clock_spi_last_echo;

#endif
