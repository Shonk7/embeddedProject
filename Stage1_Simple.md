# Stage 1: The Simple Version

## What we're doing

The chip wakes up slow, at 8 MHz, and that's nowhere near fast enough for audio. So the first job was to speed it up to 72 MHz using the crystal on the board.

Then we needed a way to prove it worked, so we made a green light blink once a second. If the chip runs at the wrong speed, the blink comes out wrong too.

## What we need

- The APE board
- A USB-C cable
- An ST-Link programmer (the small box that loads code onto the board)

## Trying it out

1. Plug the ST-Link into the 20-pin connector labelled J701, and into the computer.
2. Plug in the USB-C cable.
3. Turn the board on with the power switch.
4. Open `empty_skeleton.uvprojx` in Keil and press **F7** to build.
5. Press **F8** to load the code.
6. Press the **RESET** button on the board.

## Did it work?

First we look at the screen, which should say `72000000`. That only tells us what the chip thinks its speed is, though.

The real check is counting the green light (LED0) for one minute. This still works in the Stage 2 and Stage 4 tests.

| Flashes in one minute | What it tells us | What we do |
|---|---|---|
| **60** | **It worked** | **Move on** |
| 80 | The board has a different crystal | Change `HSE_HZ` in `clock.h` to `16000000U`, then build and load again |
| About 7 | The speed-up failed, and a red light is on | Ask a demonstrator or try another board |
| No flashing | The code isn't running | Check the J502 jumper is on the GND side, then press RESET |

We still need to do this count, because the documents disagree on whether the crystal is 12 or 16 MHz. Once we've got 60 flashes and `72000000`, Stage 1 is finished, and a photo goes in the journal.
