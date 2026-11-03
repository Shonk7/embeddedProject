# Stage 2: reading audio with the ADC

The ADC turns a voltage into a number from 0 to 4095. Before trusting it with real audio, we wanted to read the jack and put the numbers on the screen, just to see that they made sense. Silence should sit around 2048, because the board holds the audio input at 1.65 V, halfway up the 3.3 V range.

There's no sound out in this stage, so the speaker stays quiet. We also read the ADC from the main loop here, which is fine for a test. The real thing has to sample inside the interrupt, and that comes in Stage 5.

## Running it

Set `ACTIVE_TEST` to `2` at the top of `main.c`, build with **F7**, load with **F8** and press **RESET**. The screen should say `ELEC4123 STAGE 2 ADC TEST`.

Audio goes into the **J1302** jack on a 3.5 mm cable. A signal generator is the best source because you know the exact level: a 1 kHz sine at 100 mVpp, no offset, output load on High-Z. A phone or laptop works too, using a wired headphone output (Bluetooth won't). Start at the lowest volume, play a 1000 Hz sine from a tone generator app, and bring the volume up slowly.

Keep it under about 300 mVpp. The board amplifies the input 11 times, so anything bigger clips.

## What to look for

With nothing plugged in, `AVERAGE (code)` should be about 2048, or about 1650 mV. `SWING (code)` should be small, and that number is our noise level, so write it down.

With a 100 mVpp tone, the average should stay near 2048 and the swing should jump to about 1365 (1100 mV). Turn the level up and down, and the swing should follow. If you push it past 300 mVpp, `MIN` should drop towards 0 and `MAX` should climb towards 4095. That's clipping.

LED0 should flash 60 times a minute throughout, which doubles as the Stage 1 clock check. If you have a scope on PF8, you'll see a pulse about 2 µs wide for each conversion.

## What the screen is telling us

The code reads the ADC as fast as it can for half a second, shows the results, and starts again. A "code" is just the raw 0 to 4095 number, and one code is about 0.8 mV.

| Screen line | Meaning | How it's worked out |
|---|---|---|
| `MIN`, `MAX` | Lowest and highest reading in that half second | Each reading is compared against the current lowest and highest |
| `SWING (code)` | Size of the signal, bottom to top | `MAX − MIN` |
| `AVERAGE (code)` | Centre of the signal | All readings added up ÷ `CONVERSIONS` |
| `AVERAGE (mV)`, `SWING (mV)` | The same numbers in millivolts at PA3 | Code × 3300 ÷ 4095 |
| `CONV CYCLES` | CPU cycles the latest reading took | Cycle counter after − before |
| `CONVERSIONS` | Readings in that half second | Adds 1 per reading |

`MIN` and `MAX` tell us whether the signal is clipping and whether it's centred. For loudness, watch `SWING`.

It helps to follow a 100 mVpp tone all the way through. The board multiplies it by 11, so PA3 sees 1100 mV of swing around the 1650 mV centre, running from 1100 mV to 2200 mV. In codes that's 1100 ÷ 3300 × 4095 = 1365 for `MIN`, and 2200 ÷ 3300 × 4095 = 2730 for `MAX`. The swing is 2730 − 1365 = 1365, and the average stays around 2048 because the tone goes equally above and below centre. Noise will nudge the real readings a little.

A couple of quick tricks: `SWING (mV) ÷ 11` gives the real level at the jack, so it shows how strong the phone's output is. And `36,000,000 ÷ CONVERSIONS` gives the cycles per loop.

`CONV CYCLES` should be somewhere around 135 to 150. That's our estimate, not a measurement. The ADC itself takes 120: (7.5 + 12.5) ADC clocks × 6 CPU cycles each. The rest is the start delay and the code around it. `CONVERSIONS` should be around 200,000.

## The open problem

When we tried this, the readings didn't seem to change much with volume. We haven't sorted that out yet, and Stage 5 depends on it, so it needs doing first.

We need `MIN`, `MAX` and `SWING (code)` with a 1000 Hz sine for three cases: nothing plugged in, the lowest volume, and about a quarter volume. Those numbers should point to the cause:

| What we see | Likely cause |
|---|---|
| `MIN` near 0 and `MAX` near 4095 at every volume | It's already clipping, so turn down the device and the app |
| `SWING` is the same with and without sound | The sound isn't reaching the board. Check the plug, and that the sound isn't still coming from the phone's speaker |
| `SWING` is already large in silence | Noise is setting `MIN` and `MAX`, because a single spike counts |
| `SWING` moves a little per volume step | Probably fine. Compare the lowest volume with a much higher one |

## How the code works

`adc_init()` in `adc.c` turns on the clocks and makes PA3 an analog pin. It tells the ADC to convert one channel at a time, with software starting each conversion, then sets the sample time and picks channel 3. The schematic calls this net `ADC2`, which is easy to misread, but it goes to PA3, and that's channel 3.

After that, it powers up the ADC, waits a moment for it to wake (it needs 1 µs), and runs its self calibration, which the manual recommends after every power-up.

Reading a sample is three steps:

```c
adc_start();                // start a conversion
while (!adc_is_done()) {}   // wait for it to finish
code = adc_result();        // read the number
```

Waiting like that is fine in a test, but it can't work in Stage 5. At 399.25 kHz, waiting would eat most of the time we have for each sample.

The 12 MHz ADC clock is 72 MHz ÷ 6, because ÷4 would break the 14 MHz limit. We picked a sample time of 7.5 clocks, so each conversion takes 20 ADC clocks, or 120 CPU cycles, which fits inside our 180. To try other values, change `ADC_SAMPLE_TIME` in `adc.h` (`0` for 1.5 clocks, `2` for 13.5) and compare `SWING` on the same tone. If it changes much, that's worth a line in the journal.

## Where this stands

The code compiles cleanly, and `adc_init()` passed a software model of the ADC's start-up rules. The test has run on the board in Keil 5. But until the swing clearly follows the tone, we can't say the audio input works.

The facts came from the schematic (sheets 5 and 13), datasheet Table 59, and RM0008 sections 11.3.1, 11.4 and 11.6.
