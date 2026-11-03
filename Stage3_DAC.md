# Stage 3: Making Sound with the DAC

## What we're trying to do

The DAC is the opposite of the ADC. It turns a number from 0 to 4095 into a voltage on pin PA4, and that voltage ends up driving the speaker.

To prove the DAC works by itself, we made the board do two things on a repeating 18-second loop:

1. Play a 2 kHz tone through the speaker for 3 seconds.
2. Hold five fixed voltages for 3 seconds each, so we can measure them.

| Time | What the board does | What we notice |
|---|---|---|
| 0 to 3 s | Plays a beep | We hear it |
| 3 to 6 s | Holds 0.5 V | Silent |
| 6 to 9 s | Holds 1.0 V | Silent |
| 9 to 12 s | Holds 1.65 V | Silent |
| 12 to 15 s | Holds 2.5 V | Silent |
| 15 to 18 s | Holds 3.0 V | Silent |

This doesn't use the audio jack yet. Passing audio from the jack to the speaker comes in Stage 5.

## What we need

- Everything from Stage 1: USB-C cable and ST-Link
- **Optional:** a multimeter or scope to measure the voltages
- No audio source and no signal generator

## Running it

1. Connect the board the same way as Stage 1.
2. Set `ACTIVE_TEST` at the top of `main.c` to `3`.
3. In Keil, press **F7** to build, then **F8** to load.
4. Press **RESET** on the board.

The screen should say `ELEC4123 STAGE 3 DAC TEST`.

The R1303 knob is the speaker's volume. If we hear nothing, we turn it slowly, as it can be set to silent.

## What we expect to see

There's only one test running. These are three ways of checking it, from easiest to hardest.

### Check 1: listen

We should hear a steady, high-pitched beep for 3 seconds, then about 15 seconds without it, then the beep again. The screen shows `OUTPUT: 2 KHZ TONE` during the beep.

Soft clicks when the voltage changes are normal.

### Check 2: measure the voltages

Set a multimeter to **DC volts**. The red probe goes on **J1301** and the black probe on **J1303**. These are two small test loops in the speaker section, near the R1303 knob.

**Careful:** it's J1301, not J1201. J1201 is the LCD's ribbon connector, and it carries the 9.6 V backlight supply.

| Screen `TARGET (mV)` | Screen `DAC CODE` | Expected at J1301 |
|---|---|---|
| 500 | 620 | About 0.50 V |
| 1000 | 1240 | About 1.00 V |
| 1650 | 2047 | About 1.65 V |
| 2500 | 3102 | About 2.50 V |
| 3000 | 3722 | About 3.00 V |

Readings don't have to be exact. If they're all slightly high or low, we can measure the 3.3 V supply at J1101 pin 1, since the DAC output scales with it.

The R1303 knob won't change these readings, because J1301 comes before it.

During the beep, the screen shows 0 for `TARGET` and `DAC CODE`. That's only a placeholder, because the output is changing 64,000 times a second and there's no single number to show. A DC meter should read about 1.65 V then, which is the wave's average.

### Check 3: see the tone (scope only)

| Setting | Value |
|---|---|
| Channel 1 | J1301, DC coupled |
| Channel 2 | PF8 (J1101 pin 3) |
| Trigger | Channel 2, rising edge |
| Vertical | 1 V/div |
| Horizontal | 100 µs/div |

During the beep, we should see a sine wave made of 32 small steps:

| What to read | Expected |
|---|---|
| Frequency | 2.000 kHz |
| Size | About 1.6 Vpp |
| Lowest point | About 0.84 V |
| Highest point | About 2.46 V |

### Passed when

- We can hear the 2 kHz beep
- The fixed voltages are close to their targets

### Where we're up to

The test is running on the board, but **we still don't have a J1301 reading**. The screen showing a target only tells us what the code asked for, not what's on the pin. If the meter shows nothing, this is the order we'd check things:

1. **Meter setting:** it has to be on DC volts. On AC volts, a steady voltage reads about 0. On a scope, use DC coupling for the same reason.
2. **Meter and ground:** keep the black probe on J1303 and touch the red probe to J1101 pin 1, without bridging it to pin 2. It should read about 3.3 V. If not, the problem is the meter or the ground connection.
3. **Timing:** read the meter while the screen shows `OUTPUT: FIXED VOLTAGE`. Each voltage only lasts 3 seconds.
4. **The beep:** if we can hear it, the DAC works and the problem is the probes. If there's no beep and 0 V at J1301, the DAC isn't running.

**About LED0:** we commented out the LED toggle during the beep while debugging. So in this test, LED0 pauses for the 3-second beep and flashes about 50 times a minute, not 60. That isn't a clock fault. For the clock check, we use the Stage 2 or Stage 4 test instead.

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

### How we made the tone

A speaker only makes sound when the voltage moves, so a constant voltage is silent. To make a tone, we push the voltage up and down in a sine shape.

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

We traced the path on the schematic:

```text
PA4 -> J1301 test point -> R1304 -> R1303 volume knob -> 1 µF capacitor -> MAX9700 amplifier -> speaker
```

- **DAC:** makes the voltage, but it's too weak to drive a speaker.
- **R1304 and R1303:** set the volume. Even at maximum, about half the signal gets through.
- **1 µF capacitor:** blocks the steady voltage and passes only changes. That's why fixed voltages are silent and only click when they change.
- **MAX9700:** the amplifier that drives the speaker.

## If something's wrong

| Problem | Likely cause |
|---|---|
| No sound, but J1301 shows the voltages | Turn R1303. The DAC is working |
| No sound, and J1301 stays at 0 V | Check `dac_init()` runs and that `ACTIVE_TEST` is `3` |
| Screen shows a different stage | `ACTIVE_TEST` isn't `3`, or the new code didn't load |
| Keil says `dac_init` or `test_dac` is undefined | `dac.c` or `tests.c` isn't in the project |
| Scope shows the tone at 2.667 kHz, not 2.000 kHz | Crystal is 16 MHz, not 12 |
| 3000 mV step reads low | Measure the 3.3 V supply first. The DAC can't go above about 3.1 V |
| Loud clicks at each step | Normal for this test, so turn R1303 down |

## How the code works

### dac_init() in dac.c

| Step | Code | Why |
|---|---|---|
| 1 | `RCC->APB1ENR \|= DACEN` | Turns on the DAC's clock |
| 2 | `RCC->APB2ENR \|= IOPAEN` | Turns on port A's clock |
| 3 | `GPIOA->CRL &= ~(CNF4 \| MODE4)` | Makes PA4 analog. It sounds wrong for an output, but it stops digital circuits interfering with the DAC |
| 4 | `dac_write(DAC_MID_CODE)` | Starts at 1.65 V, the audio resting level |
| 5 | `DAC->CR = DAC_CR_EN1` | Turns on channel 1, with its output buffer on |

Writing a number is just one line:

```c
DAC->DHR12R1 = code;
```

`DHR12R1` means data holding register, 12-bit, right-aligned, channel 1. With no hardware trigger selected, the number reaches the pin almost straight away.

### The tests

We moved the tests to `tests.c` so that adding a stage wouldn't wipe out the one before. We pick which one runs in `main.c`:

```c
#define ACTIVE_TEST 4    // 2 = ADC, 3 = DAC, 4 = external clock
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

- All code compiles for Cortex-M3 with Clang with no warnings in our files.
- `dac_init()` ran on a computer with fake registers, and the final values matched the plan.
- The sine table, the 1,125-cycle timing and the code values were recalculated separately and matched.
- The test has been run on the board in Keil 5.

**Still to do:** record the J1301 voltages. Only hearing the beep and measuring J1301 proves the DAC works.
