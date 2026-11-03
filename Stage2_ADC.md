# Stage 2: Reading Audio with the ADC

## What we're trying to do

The ADC turns a voltage into a number from 0 to 4095, and that's how audio gets into the chip. Before trusting it with real audio, we wanted to read the jack and put the numbers on the screen, so we could see for ourselves that they make sense.

| Voltage on PA3 | Number |
|---|---|
| 0 V | 0 |
| 1.65 V (silence) | About 2048 |
| 3.3 V | 4095 |

There's no sound out yet. That comes in Stages 3 and 5.

We're reading the ADC from the main loop here, which is fine for checking it. The final audio has to sample inside the interrupt, and that's Stage 5.

## What we need

- Everything from Stage 1: USB-C cable and ST-Link
- For the sound test, one of these with a 3.5 mm cable:
  - A signal generator (best, because we can set the exact level)
  - A phone playing a tone at low volume

**The level needs to stay at 300 mVpp or less.** The board amplifies the jack input 11 times, so more than that clips.

## Running it

1. Connect the board the same way as Stage 1.
2. Set `ACTIVE_TEST` at the top of `main.c` to `2`.
3. In Keil, press **F7** to build, then **F8** to load.
4. Press **RESET** on the board.

The screen should say `ELEC4123 STAGE 2 ADC TEST`.

## Playing sound into the board

**Stage 2 makes no sound.** The speaker stays silent, because audio only goes in. We know it's working when the numbers on the screen change.

Audio goes into the **J1302 audio jack** through a 3.5 mm cable.

### Using a phone or laptop

1. Set the volume to the **lowest step**.
2. Plug a 3.5 mm cable into the headphone output and into J1302. Phones without a headphone jack need a wired adapter, and Bluetooth won't work.
3. Open a tone generator app or website, and pick a **1000 Hz sine** wave.
4. Press play and raise the volume slowly.
5. Stop when `SWING (code)` reaches about 1000 to 2000.

If `MIN` reaches 0 or `MAX` reaches 4095, it's too loud, so we turn it down. Music works too, but the numbers jump around. A steady tone gives steady readings.

### Using a signal generator

| Setting | Value |
|---|---|
| Waveform | Sine |
| Frequency | 1 kHz |
| Amplitude | 100 mVpp |
| Offset | 0 V |
| Output load | High-Z |

We can use a BNC to 3.5 mm cable, or connect the signal to the plug's tip and ground to its sleeve. This is the better option for testing, because we know the exact level going in.

## What we expect to see

The screen updates twice a second, and LED0 should flash 60 times a minute. That also doubles as the Stage 1 clock check.

### Test 1: nothing plugged in

| Screen line | Expected |
|---|---|
| `AVERAGE (code)` | About 2048 |
| `AVERAGE (mV)` | About 1650 |
| `SWING (code)` | Small. We should write this down, as it's our noise level |
| `CONV CYCLES` | Around 135 to 150 (estimate) |
| `CONVERSIONS` | Around 200,000 (estimate) |

### Test 2: play a tone

We use a 1 kHz sine at 100 mVpp. On a phone we can't set an exact level, so we just check that the swing goes up and down with the volume.

| Screen line | Expected |
|---|---|
| `AVERAGE (code)` | Still about 2048 |
| `SWING (code)` | About 1365 |
| `SWING (mV)` | About 1100 |

The 1100 mV comes from 100 mV × 11. Turning the level up or down should make the swing follow.

### Test 3: too loud

If we raise the level above 300 mVpp, `MIN` should approach 0 and `MAX` should approach 4095. That's clipping, so we turn it back down.

### Scope check (optional)

Probing PF8 (J1101 pin 3) with ground on pin 2, we should see a pulse about 2 µs wide, because PF8 is high during each conversion. That's 20 ADC clocks at 12 MHz (1.67 µs) plus a small start-up delay.

### Passed when

- Silence reads about 2048
- The swing follows the tone's level
- LED0 flashes 60 times a minute

### Where we're up to

**This one is still open.** When we tried it, the readings didn't seem to change much with volume. We still need to record `MIN`, `MAX` and `SWING (code)` with a 1000 Hz sine for three cases: nothing plugged in, the lowest volume, and about a quarter volume. We need to sort this out before Stage 5, because that stage depends on audio getting in.

| What we might see | Likely cause | What we'd do |
|---|---|---|
| `MIN` near 0 and `MAX` near 4095 at every volume | Already clipping | Turn down both the device and the app volume |
| `SWING` is about the same with sound as with silence | The sound isn't reaching the board | Push the plug fully in, and check the sound isn't still coming from the phone's speaker |
| `SWING` is already large with silence | Noise is setting `MIN` and `MAX`, because a single spike counts | Compare with the plug out, and use a steady tone |
| `SWING` changes a little per volume step | Probably fine | Compare the lowest volume with a much higher one |

## What the numbers mean

For half a second, the code reads the ADC over and over and keeps track of the results. Then it shows these values and starts again.

A **code** is the raw ADC number from 0 to 4095. One code is about 0.8 mV.

| Screen line | What it means | How it's calculated |
|---|---|---|
| `SYSCLK (Hz)` | CPU speed the registers report | Read from the clock settings |
| `MIN (code)` | Lowest reading in that half second | Each reading is compared with the lowest so far |
| `MAX (code)` | Highest reading in that half second | Each reading is compared with the highest so far |
| `SWING (code)` | How big the signal is, from bottom to top | `MAX − MIN` |
| `AVERAGE (code)` | The signal's centre | All readings added up ÷ `CONVERSIONS` |
| `AVERAGE (mV)` | The same centre, in millivolts | `AVERAGE × 3300 ÷ 4095` |
| `SWING (mV)` | Signal size at PA3, in millivolts | `SWING × 3300 ÷ 4095` |
| `CONV CYCLES` | CPU cycles the latest reading took | Cycle counter after − before |
| `CONVERSIONS` | Readings taken in that half second | Adds 1 per reading |

The division drops any decimals, so 2047.9 shows as 2047.

`MAX` and `MIN` show us the top and bottom of the signal. They tell us whether it's clipping and whether it's centred. For loudness, `SWING` is the one to watch.

### Worked example

Let's follow a 100 mVpp tone from the jack to the screen.

| Step | Calculation | Result |
|---|---|---|
| The board amplifies it | 100 mV × 11 | 1100 mV swing at PA3 |
| It sits around the 1650 mV centre | 1650 − 550 and 1650 + 550 | From 1100 mV to 2200 mV |
| Lowest reading | 1100 ÷ 3300 × 4095 | `MIN` = 1365 |
| Highest reading | 2200 ÷ 3300 × 4095 | `MAX` = 2730 |
| Swing | 2730 − 1365 | `SWING` = 1365 |
| Average | The tone goes equally above and below centre | `AVERAGE` = about 2048 |
| Back to millivolts | 1365 × 3300 ÷ 4095 | `SWING (mV)` = 1100 |

Noise makes real readings slightly different from these.

### Handy calculations

- **Level at the jack:** `SWING (mV) ÷ 11`. This tells us how strong the phone's output really is.
- **Tone size without the noise:** tone `SWING` minus silent `SWING`, roughly.
- **Readings per second:** `CONVERSIONS × 2`.
- **Cycles per loop:** `36,000,000 ÷ CONVERSIONS`, since half a second is 36 million cycles at 72 MHz.

### What `CONV CYCLES` should be

The ADC itself takes 120 cycles: (7.5 + 12.5) ADC clocks × 6 CPU cycles each. The reading comes out a bit higher because it also includes the start delay and the instructions around the conversion.

`CONVERSIONS` is lower than a pure ADC rate because each loop also spends time updating `MIN`, `MAX` and the total.

## If something's wrong

| Problem | Likely cause |
|---|---|
| Screen is black or stops at `SYSCLK`, and LED0 doesn't flash | The code is stuck waiting for the ADC. Check `adc_init()` still sets `ADC1EN` and that `SystemInit` still sets the ADC divider |
| Keil says `adc_init` is undefined | `adc.c` isn't in the project. Add it under Sources |
| Average reads about 0 or 4095 | Wrong channel or pin. Should be channel 3 on PA3 |
| Swing doesn't change with sound | See "Where we're up to" above |
| Swing is much bigger than expected | Level too high, or the cable is picking up noise |
| LED0 flashes 80 times a minute | Crystal is 16 MHz. Change `HSE_HZ` in `clock.h` to `16000000U` |

## How the code works

### adc_init() in adc.c

| Step | Code | Why |
|---|---|---|
| 1 | `RCC->APB2ENR \|= IOPAEN \| ADC1EN` | Turns on the clocks for port A and the ADC |
| 2 | `GPIOA->CRL &= ~(CNF3 \| MODE3)` | Makes PA3 an analog pin |
| 3 | `ADC1->CR1 = 0` | One channel, no interrupts |
| 4 | `ADC1->CR2 = EXTTRIG \| EXTSEL = 111` | Lets software start each conversion |
| 5 | `ADC1->SMPR2` | Sets the sample time for channel 3 |
| 6 | `ADC1->SQR1 = 0` | One conversion per start |
| 7 | `ADC1->SQR3 = 3` | That conversion reads channel 3 |
| 8 | `CR2 \|= ADON` | Powers up the ADC |
| 9 | Short delay | The ADC needs 1 µs to wake up |
| 10 | `RSTCAL`, then `CAL` | The ADC calibrates itself, which improves accuracy |

### Reading one sample

```c
adc_start();                // start a conversion
while (!adc_is_done()) {}   // wait for it to finish
code = adc_result();        // read the number
```

Waiting like this is fine in a test. Stage 5 can't wait, because at 399.25 kHz that would use up most of the time for each sample.

### Why we picked this speed and sample time

- **ADC clock is 12 MHz.** That's 72 MHz ÷ 6. The limit is 14 MHz, and ÷4 would give 18 MHz.
- **Sample time is 7.5 clocks.** Each conversion takes 7.5 + 12.5 = 20 ADC clocks, or 120 CPU cycles.
- **That fits.** At 399.25 kHz we get 180 cycles per sample.

To try another sample time, we can change `ADC_SAMPLE_TIME` in `adc.h`, then compare `SWING` on the same tone. `0` gives 1.5 clocks and `2` gives 13.5 clocks. If the swing or noise changes a lot, that's useful for the journal.

### The test in tests.c

The test lives in `test_adc()` in `tests.c`. For half a second, the loop reads the ADC as fast as it can and tracks the lowest, highest and average values. Then it prints them and toggles LED0. The screen only updates between readings.

## How we know it works

| Claim | How we know |
|---|---|
| Channel 3 is the audio jack | Schematic sheets 5 and 13. The net is called `ADC2`, but it goes to PA3, which is channel 3 |
| Silence sits at 1.65 V | Schematic: two 1 MΩ resistors bias the amplifier at half of 3.3 V |
| Gain is 11 | Schematic: 1 + 100k/10k |
| 12 MHz and 14 MHz limit | Datasheet Table 59 |
| Calibration and start-up order | RM0008 sections 11.3.1 and 11.4 |
| Conversion takes 20 ADC clocks | RM0008 section 11.6 |

**Checked so far:**

- All code compiles for Cortex-M3 with Clang, with no warnings in our files.
- `adc_init()` ran against a software model of the ADC's start-up rules with no violations, and the model caught a deliberately broken version.
- The final register values matched the plan, and the screen number formatting was tested on a computer.
- The test has been run on the board in Keil 5.

**Still to do:** record real readings. Until the swing clearly follows the tone's level, we can't say the audio input works.
