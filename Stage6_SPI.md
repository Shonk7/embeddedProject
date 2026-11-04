# Stage 6: talking to the clock board over SPI

The clock board picks its rate from a one-byte command we send it: `D1`, `D2`, `D4` or `D8` for the four rates, `E0` to stop and `E1` to start. This stage is the bit of firmware that actually sends those bytes, and it's also where our code, the UI and the clock board finally meet.

The UI team had already done most of the work. Their `ui/rate_ctrl.c` works out which byte to send when a button is pressed, and it had two commented-out calls to a `clock_spi_send()` function waiting for this module. We wrote that function in `Sources/spi/clock_spi.c` and switched those two lines on.

## Wiring the clock board

The board plugs into **J901**, one of the four SPI expansion headers:

| J901 pin | Signal | Goes to on the clock board |
|---|---|---|
| 1 | GND | Ground |
| 2 | 3.3 V | Power (the spec says the board must run off the APE's 3.3 V) |
| 3 | MISO | Nothing. The link only goes one way |
| 4 | SCK (PA5) | Shift clock, the 74HC595's SRCLK if that's what you used |
| 5 | MOSI (PA7) | Data in, the 595's SER |
| 6 | CS (PC1) | Latch, the 595's RCLK |

The clock board's output still goes to **J1101 pin 6** (PF11), with ground on pin 8, the same as the signal generator did in Stages 4 and 5.

Here's what the clock board has to agree with:

- **Mode 0:** the clock idles low, and data is read on each rising edge, which is what a 74HC595 does.
- **MSB first:** with a 595, that means after 8 clocks QH holds bit 7 and QA holds bit 0. So `D1` arrives as QH to QA = 1101 0001. If the decoder reads the bits the other way round, every command will look wrong.
- **CS goes low before the byte and back high after it.** That rising edge is the moment to latch. Latching anywhere else would let half-shifted bytes through, which is exactly the glitch the spec says invalid commands mustn't cause.
- **Speed is 281 kHz** (72 MHz ÷ 256), which is slow on purpose, because breadboard wires are forgiving at that speed.

SCK and MOSI also go to the touch controller and the EEPROM, so the clock board's shift register will see their traffic too. That's fine as long as it only latches on its own CS rising. The last 8 bits before that rising edge are always ours.

## Testing before the clock board exists

We don't need the clock board to know the SPI side works. There are three ways to check it, easiest first.

### The loopback jumper (`ACTIVE_TEST` 9)

This one needs nothing but a female-to-female jumper wire. Connect **J901 pin 5 (MOSI) to J901 pin 3 (MISO)**. SPI sends and receives at the same time, so with that wire in place, every bit we send comes straight back in. The test sends 0 to 255 over and over and checks each byte as it returns.

Pins 1, 3 and 5 sit in one column, and pin 1 is ground, so make sure the jumper doesn't touch it. A driven MOSI pin shorted to ground isn't good for the chip.

Set `ACTIVE_TEST` to `9`, build, load and press **RESET**. With the jumper in, the screen should show `ERRORS` stuck at 0, `LAST SENT` and `LAST ECHO` matching, and a green `PASS`. `BYTES SENT` should climb by roughly 16,000 to 17,000 every half second, because each byte takes about 30 µs once the code around it is counted.

Now pull the jumper out. It should switch to a red `FAIL` and the errors should climb, because nothing is driving MISO anymore. That's worth doing, because it proves the test can actually fail.

A pass tells us SPI1 is switched on, the pins are set up properly, and every byte actually goes out of the chip and finishes. It can't tell us about the chip select or the bit order, because a byte that went out backwards would also come back backwards. The scope covers those.

The jumper is safe with the touch controller and EEPROM on the same bus, because both leave MISO alone while their chip selects are high, and `clock_spi_init()` makes sure they are.

### The scope

Run test 8 or 9 and probe **J1701 pin 5** (SCK), **J1701 pin 9** (MOSI) and **J901 pin 6** (CS), with ground on an even pin of J1701. Test 8 is easier to watch, because it sends just one byte a second. Trigger on CS falling.

You should see CS drop, eight clock pulses about 3.6 µs apart, and CS rise again once the eighth one finishes. The whole byte takes about 28 µs. Test 8 starts with `E1`, then `D1`, and `D1` on MOSI reads 1, 1, 0, 1, 0, 0, 0, 1 at the eight rising clock edges. That's the bit order the clock board will see.

Test 8 itself is fine to run with nothing connected. Every rate will just read 0, which shows the SPI side doesn't hang waiting for anything.

### Through the UI, with the debugger

To check the touchscreen really sends bytes, run the full system (`ACTIVE_TEST` 4) in Keil's debugger instead of just flashing it. Use **Debug → Start/Stop Debug Session** and add `clock_last_cmd` and `clock_spi_last_echo` to a Watch window. Turn on **View → Periodic Window Update** so they refresh while it runs.

Every tap on a rate button or START/STOP should change `clock_last_cmd` to the right byte, and the rate LEDs PF0 to PF4 should follow along. With the loopback jumper still in, `clock_spi_last_echo` should match it each time, which proves the byte left the chip and isn't just a value set in memory.

## Test 1: SPI by itself (`ACTIVE_TEST` 8)

This test leaves the UI out, so if something's wrong, we know it's the link or the clock board. It sends each command in turn, waits half a second for the board to settle, measures the rate coming back on PF11 for half a second, and prints it next to the command. Every step takes a second, and the whole list repeats.

Set `ACTIVE_TEST` to `8`, build, load and press **RESET**. The screen says `ELEC4123 STAGE 6 SPI TEST`. This is what a working clock board should show:

| Sent | Rate (Hz) should be about | Why |
|---|---|---|
| `E1 ENABLE` | Whatever rate the board was on | Starting from an unknown state |
| `D1 FX` | 399250 | |
| `D2 FX/2` | 199625 | |
| `D4 FX/4` | 99812 | |
| `D8 FX/8` | 49906 | |
| `55 BAD` | 49906 | Invalid: nothing should change |
| `D3 BAD` | 49906 | Invalid |
| `FF BAD` | 49906 | Invalid |
| `00 BAD` | 49906 | Invalid |
| `E0 DISABLE` | 0 | Clock stopped |
| `E2 BAD` | 0 | Invalid, so it stays stopped |
| `E1 ENABLE` | 49906 | Back on fx/8, so the rate was held across the disable |

The real numbers will be a bit off, because the clock board's oscillator won't sit exactly on 399.25 kHz. The spec allows ±50%. What matters is the pattern: each rate roughly half the one before, the bad bytes changing nothing, and `E1` bringing back the same rate.

Audio passes through during this test as well, so plugging in a tone lets you hear each rate change.

That one table covers three demo requirements: the measured rate at each setting, invalid commands being ignored, and the rate surviving a disable.

## Test 2: the full system (`ACTIVE_TEST` 4)

This is the real thing: the team's touchscreen, our audio passthrough, and the clock board, all at once. `main.c` starts the audio first, then `rate_init()` puts the clock board in a known state (off, fx/8, on), and then the UI takes over.

Tap a rate button, or use **PB0** to step through rates and **PB1** to start and stop the clock. The rate LEDs follow along: PF0 lights for fx, PF1 for fx/2, PF2 for fx/4 and PF3 for fx/8, and PF4 is on while the clock runs. With a tone going into J1302, you should hear the pitch of the sampling artefacts change with each rate, and the sound should stop when you press STOP CLOCK.

## Looking at the bytes on a scope

The test port makes this easy. **J1701 pin 5** is SCK and **pin 9** is MOSI, with ground on any even pin. CS is on **J901 pin 6**. Trigger on CS falling, and you should see CS go low, eight clock pulses at about 3.6 µs each, and the data bits on MOSI, then CS go back high.

For `D1`, MOSI reads high, high, low, high, low, low, low, high on the eight rising edges. If the scope shows that and the clock board still doesn't respond, the problem is on the board's side.

## How the code works

`clock_spi_init()` sets up the pins. It holds every SPI chip select high (PC0 for the EEPROM, PC1 for the clock board, PC5 for touch) so no other device mistakes our bytes for its own. It also sets PA5 and PA7 to their SPI function and PA6 as an input. It's safe to call more than once, and `clock_spi_send()` calls it automatically the first time.

`clock_spi_send()` does this for each byte:

1. Saves SPI1's current settings and swaps in ours. The touch driver uses SPI1 too, with different settings, so whoever uses it puts back what was there before.
2. Pulls CS low and writes the byte.
3. Waits for all 8 bits to go out, and reads back the byte that came in, which is just junk because the board can't talk back.
4. Raises CS once SPI1 is no longer busy, so the latch only happens after the last clock edge.
5. Puts the touch driver's settings back.

It waits for the byte, which takes about 30 µs, and that's fine because it only runs from the main loop. The audio ISR can interrupt it at any point, and the SPI hardware simply carries on. What must never happen is the touch driver and this function using SPI1 at the same time, which is why both stay in the main loop.

## What changed when we brought in the UI

The UI lives on the team's `main` branch, so we copied their files into our working copy. That's `Sources/ui/`, their `.gitignore`, their `debug.c` change and their Keil project files. Then we combined `main.c` by hand. Nothing has been committed or merged in git yet.

Their `debug.c` moved the heartbeat LED from PF0 to PF5 so the rate LEDs could have PF0 to PF4. So the LED to count in the bench tests is now **LED5**, and the earlier guides have been updated to match.

Both sides had used test numbers 4 and 5 for different things, so `ACTIVE_TEST` is now:

| Number | Runs |
|---|---|
| 2 | ADC test (Stage 2) |
| 3 | DAC test (Stage 3) |
| **4** | **Full system: touchscreen, audio and clock board** |
| 5 | Touch calibration (UI team) |
| 6 | Audio stats (Stage 5) |
| 7 | External clock count (Stage 4) |
| 8 | SPI command test (Stage 6) |
| 9 | SPI loopback, no clock board needed (Stage 6) |

## If something's wrong

| Problem | Likely cause |
|---|---|
| Every rate reads 0 | The clock board's output isn't on J1101 pin 6, it's not powered, or it never got `E1` |
| The rate never changes | Check SCK, MOSI and CS on a scope. If the bytes look right, the board's decoder or latch is the issue |
| Rates come out in the wrong order | Bit order mismatch. The board is reading LSB first |
| Bad bytes change the rate | The board's invalid-command logic isn't working, which is a hardware fix |
| Touch stops working after a rate change | SPI1 settings weren't put back. Check `clock_spi_send()` still restores them |
| Screen frozen on the SPI test | The SPI clock isn't on. Check `clock_spi_init()` sets `SPI1EN` |
| Keil says `clock_spi_send` is undefined | `Sources/spi/clock_spi.c` isn't in the project |

## Where this stands

Everything compiles together cleanly at every `ACTIVE_TEST` setting: our code, the UI team's code, and this module. We also ran `clock_spi_send()` against simulated registers. Every byte went out with CS low, in mode 0, MSB first, at 72 MHz ÷ 256, and CS only rose once SPI1 had finished. The touch driver's settings came back every time, and two deliberately broken versions were caught. With MOSI wired back to MISO in the model, all 256 byte values came back unchanged.

**Nothing in this stage has run on the board yet**, and we haven't connected the real clock board. The test 8 table is what will prove it.

The register details came from RM0008 section 25 (SPI), and the pins and headers from schematic sheets 5 and 9. The command set and invalid-command rules are in the task spec, page 4.
