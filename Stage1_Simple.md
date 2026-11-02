# Stage 1: The Simple Version

## What it does

The chip starts slow, at 8 MHz. Our code speeds it up to 72 MHz, which is fast enough for audio.

Then it blinks a green light once per second so we can check the speed is right.

## What you need

- The APE board
- A USB-C cable
- An ST-Link programmer (the small box that loads code onto the board)

## Steps

1. Plug the ST-Link into the 20-pin connector labelled J701, and into the computer.
2. Plug in the USB-C cable.
3. Turn the board on with the power switch.
4. Open the project in Keil and press **F7** to build.
5. Press **F8** to load the code.
6. Press the **RESET** button on the board.

## Check it worked

**Look at the screen.** It should say `72000000`.

**Count the green light (LED0) for one minute.**

| Flashes in one minute | What it means | What to do |
|---|---|---|
| **60** | **It worked** | **Move to Stage 2** |
| 80 | The board has a different crystal | Change `HSE_HZ` in `clock.h` to `16000000U`, then build and load again |
| About 7 | The speed-up failed, and a red light is on | Ask for help or try another board |
| No flashing | The code isn't running | Check the J502 jumper is on the GND side, then press RESET |

## Done

If you counted 60 flashes and the screen says `72000000`, Stage 1 is finished. Take a photo for the journal.
