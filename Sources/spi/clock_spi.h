#ifndef CLOCK_SPI_H
#define CLOCK_SPI_H

#include <stdint.h>

// Sends command bytes to the clock board (Breadboard Build Guide rev 8).
// Wiring, APE side:
//   J901 pin 4  PA5  SCLK       -> U4/U5 pin 11
//   J901 pin 5  PA7  MOSI       -> U4/U5 pin 14
//   J901 pin 6  PC1  CMD_LATCH  -> U4 pin 12     idle LOW, pulsed HIGH
//   J1101 pin 5 PF10 APPLY      -> U8 pin 2      idle LOW, held HIGH
//   J901 pin 2  3.3 V, J901 pin 1 GND
// Sample clock back from the board: J1101 pin 6 (PF11), see extclk.h.
// The board docs specified APPLY on J902 pin 6 (PC2) and the sample clock on
// J1104 pin 7 (PG1); the team moved both to J1101. PC2 is no longer used.
// PC1 and PF10 are plain GPIO strobes, NOT active-low chip selects.
//
// One command = one transaction (board doc "transaction and reset sequence"):
//   1. APPLY LOW for >= 40 us, SCLK and CMD_LATCH LOW
//   2. 8 bits, MSB first, SPI mode 0 (SCLK idles LOW, data read on rising edge)
//   3. wait >= 2 us, CMD_LATCH HIGH >= 2 us, LOW, wait >= 2 us
//   4. APPLY HIGH >= 40 us, nothing else on the SPI bus
//   5. APPLY LOW, wait >= 40 us before any more SPI traffic
// The whole transaction blocks for about 190 us. Main loop only: the touch
// driver shares SCLK/MOSI and would overwrite U5's shift register mid-command

// Sets up PC1/PF10 (LOW), the SPI1 pins, and holds the EEPROM and touch chip
// selects HIGH. Safe to call more than once
void clock_spi_init(void);

// Sends one byte as a complete transaction. Any byte goes out, including
// invalid ones, so the demo can show the board ignoring them. Returns the byte
// received on MISO at the same time: meaningless with the clock board (it never
// talks back), but it is useful for a MOSI-to-MISO loopback check
uint8_t clock_spi_send(uint8_t cmd);

#endif
