# Stage 3: making sound with the DAC

The DAC is the ADC in reverse. It takes a number from 0 to 4095 and puts out a voltage on PA4, which ends up driving the speaker. We wanted to prove it worked by itself before adding any audio, so this test doesn't touch the jack.

The board runs through an 18-second loop. It beeps at 2 kHz for 3 seconds, then holds five fixed voltages for 3 seconds each so we can measure them: 0.5, 1.0, 1.65, 2.5 and 3.0 V.

## Running it

Set `ACTIVE_TEST` to `3` at the top of `main.c`, build with **F7**, load with **F8** and press **RESET**. The screen should say `ELEC4123 STAGE 3 DAC TEST`. You don't need an audio source or a signal generator for this one.

If you can't hear anything, turn the R1303 knob slowly. That's the speaker volume, and it can be set to silent.

## Checking it

The easy check is listening. You should get a steady high-pitched beep, about 15 seconds of quiet, then the beep again. Soft clicks when the voltage changes are normal.

For proper numbers, set a multimeter to **DC volts**, with red on **J1301** and black on **J1303**. They're two small test loops near the R1303 knob. Be careful with the label: J1201 is the LCD's ribbon connector, and it carries the 9.6 V backlight supply.

| Screen `TARGET (mV)` | Screen `DAC CODE` | Meter should read about |
|---|---|---|
| 500 | 620 | 0.50 V |
| 1000 | 1240 | 1.00 V |
| 1650 | 2047 | 1.65 V |
| 2500 | 3102 | 2.50 V |
| 3000 | 3722 | 3.00 V |

They don't need to be exact. If they're all a little high or low, check the 3.3 V supply at J1101 pin 1, because the DAC scales with it. The R1303 knob won't change these readings, since J1301 comes before it.

During the beep, the screen shows 0 for both numbers. That's only a placeholder: the output changes 64,000 times a second, so there's no single value to show. A DC meter should read about 1.65 V then, the average of the wave.

With a scope, put channel 1 on J1301 (DC coupled) and channel 2 on PF8, and trigger on PF8's rising edge at about 100 µs/div. During the beep you should see a 2.000 kHz sine made of 32 tiny steps, about 1.6 Vpp, running from about 0.84 V to 2.46 V.

## Where this stands

The test is running on the board, but **we still don't have a J1301 reading**. The screen showing a target only tells us what the code asked for, not what's on the pin.

If the meter shows nothing, start with the setting. On AC volts a steady voltage reads about 0, and a scope on AC coupling does the same. Next, keep black on J1303 and touch red to J1101 pin 1, without bridging it to pin 2. It should read 3.3 V, and if it doesn't, the problem is the meter or the ground. If that's fine, read J1301 while the screen says `OUTPUT: FIXED VOLTAGE`, because each voltage only lasts 3 seconds.

The beep is a useful tie-breaker. If you can hear it, the DAC works and it's a probe problem. No beep and 0 V at J1301 means the DAC isn't running.

One more thing: the LED toggle during the beep is commented out for debugging. So in this test LED0 pauses for 3 seconds and flashes about 50 times a minute, not 60. That isn't a clock fault, so use the Stage 2 or Stage 4 test for the clock check.

## How the tone is made

A speaker only makes sound when the voltage moves, which is why the fixed voltages are silent. To get a tone, we push the voltage up and down in a sine shape.

We store one wave as 32 points in a table. At 2 kHz, that's 32 × 2,000 = 64,000 DAC updates a second, or one every 72,000,000 ÷ 64,000 = 1,125 CPU cycles. We chose 2 kHz because it's close to the speaker's loudest point, about 1.7 kHz.

The table runs from 2048 − 1000 to 2048 + 1000. Turning those codes into voltage with `code × 3300 ÷ 4095` gives 0.84 V and 2.46 V, so the wave is about 1.6 Vpp. One code is about 0.8 mV, same as the ADC.

The fixed voltages go the other way: `TARGET × 4095 ÷ 3300`. The division drops decimals, which is why 1650 mV comes out as code 2047 and not 2047.5.

## From the pin to the speaker

Tracing it on the schematic, PA4 goes to the J1301 test point, through R1304 and the R1303 knob, then a 1 µF capacitor, into the MAX9700 amplifier, and out to the speaker.

The DAC is too weak to drive a speaker, so the MAX9700 does that part. The resistors and knob set the volume, and even at maximum only about half the signal gets through. The capacitor blocks steady voltage and only passes changes, which is why fixed voltages are silent and just click when they change.

## How the code works

`dac_init()` in `dac.c` is short. It turns on the clocks for the DAC and port A, and makes PA4 analog. That sounds wrong for an output, but the DAC needs the pin's digital side out of the way. Then it sets the output to 1.65 V, the resting level for audio, and turns on channel 1.

Writing a value is a single line:

```c
DAC->DHR12R1 = code;
```

The name means data holding register, 12-bit, right-aligned, channel 1. With no hardware trigger set up, the number reaches the pin almost straight away.

We moved the tests into `tests.c` here, so that adding a stage wouldn't wipe out the one before it. `ACTIVE_TEST` in `main.c` picks which one runs.

## If something's wrong

| Problem | Likely cause |
|---|---|
| No sound, but J1301 shows the voltages | Turn R1303. The DAC is fine |
| No sound and 0 V at J1301 | Check `ACTIVE_TEST` is `3` and that `dac_init()` runs |
| Keil says `dac_init` is undefined | `dac.c` isn't in the project |
| Scope shows 2.667 kHz, not 2.000 kHz | Crystal is 16 MHz |
| The 3000 mV step reads low | Check the 3.3 V supply. The DAC tops out around 3.1 V |
| Loud clicks | Normal for this test. Turn R1303 down |

The code compiles cleanly, `dac_init()` gave the right register values on simulated hardware, and the sine table and timing checked out when we recalculated them. The facts came from Board Guide Ch 3, schematic sheets 5 and 13, datasheet Table 63, and RM0008 sections 12.3.4 and 12.3.5.
