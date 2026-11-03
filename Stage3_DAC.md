# Stage 3: Making Sound with the DAC

## What it does

The DAC does the opposite of the ADC. It turns a number from 0 to 4095 into a voltage on pin PA4, and that voltage drives the speaker.

This stage makes the board do two things, on a repeating 18-second loop:

1. Play a 2 kHz tone through the speaker for 3 seconds.
2. Hold five fixed voltages for 3 seconds each, so you can measure them.

This test doesn't use the audio jack yet. Passing audio from the jack to the speaker is Stage 5.

## What you need

- Everything from Stage 1: USB-C cable and ST-Link
- **Optional:** a multimeter or scope to measure the voltages
- No audio source and no signal generator

## Steps

1. Connect the board the same way as Stage 1.
2. In Keil, check `dac.c`, `dac.h`, `tests.c` and `tests.h` are listed under Sources. If not, right-click Sources, choose **Add Files to Group 'Sources'**, and add them.
3. Check `ACTIVE_TEST` at the top of `main.c` is `3`.
4. Press **F7** to build.
5. Press **F8** to load the code.
6. Press **RESET** on the board.

The R1303 knob is the speaker's volume. If you hear nothing, turn it slowly, as it can be set to silent.

## Check it worked

### Test 1: listen (no equipment needed)

You should hear a steady, high-pitched beep for 3 seconds, then about 15 seconds without it, then the beep again. The screen shows `OUTPUT: 2 KHZ TONE` during the beep.

You'll also hear soft clicks when the voltage changes. That's normal for this test.

### Test 2: measure the voltages

Connect a multimeter or scope to **J1301**, with ground on **J1303**. Read the voltage while the screen shows each target.

| Screen `TARGET (mV)` | Screen `DAC CODE` | Expected at J1301 |
|---|---|---|
| 500 | 620 | About 0.50 V |
| 1000 | 1240 | About 1.00 V |
| 1650 | 2047 | About 1.65 V |
| 2500 | 3102 | About 2.50 V |
| 3000 | 3722 | About 3.00 V |

Readings don't have to be exact. If they're all slightly high or low, measure the 3.3 V supply at J1101 pin 1, since the DAC output scales with it.

The R1303 knob won't change these readings, because J1301 comes before it.

### Test 3: see the tone (scope only)

| Setting | Value |
|---|---|
| Channel 1 | J1301, DC coupled |
| Channel 2 | PF8 (J1101 pin 3) |
| Trigger | Channel 2, rising edge |
| Vertical | 1 V/div |
| Horizontal | 100 µs/div |

During the beep, you should see a sine wave made of 32 small steps:

| What to read | Expected |
|---|---|
| Frequency | 2.000 kHz |
| Size | About 1.6 Vpp |
| Lowest point | About 0.84 V |
| Highest point | About 2.46 V |

### Passed when

- You can hear the 2 kHz beep
- The fixed voltages are close to their targets
- LED0 flashes 60 times a minute

## What the numbers mean

| Screen line | What it means | How it's calculated |
|---|---|---|
| `SYSCLK (Hz)` | CPU speed the registers report | Read from the clock settings |
| `OUTPUT` | What the DAC is doing now | Set by the test |
| `TARGET (mV)` | The voltage we're asking for. Shows 0 during the beep | Fixed list in the code |
| `DAC CODE` | The number sent to the DAC. Shows 0 during the beep | `TARGET × 4095 ÷ 3300` |

The division drops decimals, so the code for 1650 mV is 2047, not 2047.5.

### Going from code to voltage

`Voltage = code × 3300 ÷ 4095`

For code 3102: 3102 × 3300 ÷ 4095 = about 2500 mV.

One code step is about 0.8 mV, the same as the ADC.

### How the tone is made

The speaker makes sound when the voltage moves, so a constant voltage is silent. To make a tone, the code sends the voltage up and down in a sine shape.

| Item | Calculation | Result |
|---|---|---|
| Points in one sine wave | Chosen | 32 |
| Tone frequency | Chosen, close to the speaker's loudest point | 2,000 Hz |
| DAC updates per second | 32 × 2,000 | 64,000 |
| CPU cycles between updates | 72,000,000 ÷ 64,000 | 1,125 |
| Highest code | 2048 + 1000 | 3048, about 2.46 V |
| Lowest code | 2048 − 1000 | 1048, about 0.84 V |
| Size of the wave | 3048 − 1048 = 2000 codes × 0.806 mV | About 1.6 Vpp |

## How the sound reaches the speaker

```text
PA4 -> J1301 test point -> R1304 -> R1303 volume knob -> 1 µF capacitor -> MAX9700 amplifier -> speaker
```

- **DAC:** makes the voltage, but it's too weak to drive a speaker.
- **R1304 and R1303:** set the volume. Even at maximum, about half the signal gets through.
- **1 µF capacitor:** blocks the steady voltage and passes only changes. That's why fixed voltages are silent and only make a click when they change.
- **MAX9700:** the amplifier that drives the speaker.

## If something's wrong

| Problem | Likely cause |
|---|---|
| No sound, but J1301 shows the voltages | Turn R1303. The DAC is working |
| No sound, and J1301 stays at 0 V | Check `dac_init()` runs and that `ACTIVE_TEST` is `3` |
| Screen still says STAGE 2 | `ACTIVE_TEST` is `2`, or the new code didn't load |
| Keil says `dac_init` or `test_dac` is undefined | `dac.c` or `tests.c` isn't in the project |
| Scope shows the tone at 2.667 kHz, not 2.000 kHz | Crystal is 16 MHz, not 12 |
| 3000 mV step reads low | Measure the 3.3 V supply first. The DAC can't go above about 3.1 V |
| Loud clicks at each step | Normal for this test, so turn R1303 down |

If LED0 flashes 80 times a minute, change `HSE_HZ` in `clock.h` to `16000000U`.

## How the code works

### dac_init() in dac.c

| Step | Code | Why |
|---|---|---|
| 1 | `RCC->APB1ENR \|= DACEN` | Turns on the DAC's clock |
| 2 | `RCC->APB2ENR \|= IOPAEN` | Turns on port A's clock |
| 3 | `GPIOA->CRL &= ~(CNF4 \| MODE4)` | Makes PA4 analog. It sounds wrong for an output, but it stops digital circuits interfering with the DAC |
| 4 | `dac_write(DAC_MID_CODE)` | Starts at 1.65 V, the audio resting level |
| 5 | `DAC->CR = DAC_CR_EN1` | Turns on channel 1, with its output buffer on |

Writing a number is one line:

```c
DAC->DHR12R1 = code;
```

`DHR12R1` means data holding register, 12-bit, right-aligned, channel 1. With no hardware trigger selected, the number reaches the pin almost straight away.

### The tests

The tests moved to `tests.c` so that Stage 2's test isn't lost. Pick which one runs in `main.c`:

```c
#define ACTIVE_TEST 3    // 2 = ADC, 3 = DAC
```

`test_dac()` loops through the beep and the five voltages. `dac_play_tone()` waits 1,125 cycles, sends the next value from a 32-entry sine table, and repeats.

## How we know it works

| Claim | How we know |
|---|---|
| PA4 is DAC channel 1, and PA4 should be analog | Board Guide Ch 3, schematic sheet 5 |
| Voltage formula | Board Guide Ch 3, RM0008 section 12.3.5 |
| Output path to the speaker | Schematic sheet 13 |
| Buffered output reaches about 0.2 V to 3.1 V | Datasheet Table 63 |
| Wake-up takes up to 10 µs | Datasheet Table 63 |
| Number reaches the output one APB1 clock after writing | RM0008 section 12.3.4 |
| Speaker's loudest point is about 1.7 kHz | Board Guide Ch 3 |

**Checked so far:**

- All code compiles for Cortex-M3 with Clang with no warnings in our files, for both test settings.
- `dac_init()` ran on a computer with fake registers, and the final values matched the plan.
- The sine table, the 1,125-cycle timing and the code values were recalculated separately and matched.

**Not done yet:** a Keil build, and a run on the real board. Only hearing the beep and measuring J1301 proves it works.
