# Stage 1, the short version

The chip boots at 8 MHz, which is far too slow for audio, so the first thing we did was get it running at 72 MHz off the crystal on the board. To check it worked, we blink LED5 once a second. If the speed is wrong, the blink is wrong too.

You'll need the board, a USB-C cable and an ST-Link (the little box that loads code onto the board).

Plug the ST-Link into the 20-pin connector J701 and into the computer, then plug in USB-C and turn the board on with the power switch. Open `empty_skeleton.uvprojx` in Keil, press **F7** to build and **F8** to load, then press **RESET** on the board.

The screen should say `72000000`, but that's only what the chip believes about itself. The real test is counting LED5 for a minute with `ACTIVE_TEST` set to 2, 6 or 7:

| Flashes per minute | What it means | What to do |
|---|---|---|
| 60 | It worked | Move on |
| 80 | The crystal is 16 MHz, not 12 | Set `HSE_HZ` in `clock.h` to `16000000U`, then rebuild |
| About 7 | It never got past 8 MHz (the red light is on too) | Ask a demonstrator or try another board |
| None | The code isn't running | Check jumper J502 is on the GND side, then press RESET |

We still need to do this count, because the documents disagree about the crystal. When we get 60, take a photo for the journal and Stage 1 is done.
