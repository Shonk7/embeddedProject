# Stage 5: audio in, audio out

This is the stage where it starts being an audio system. Sound goes into the jack, gets sampled inside the external clock's interrupt, and comes out of the speaker. There's no processing yet, which is exactly what the spec wants on the main page.

## How each interrupt works

The obvious way to write this is to start the ADC, wait for it, and send the result to the DAC. That doesn't work at our top rate. We get 180 CPU cycles per sample at 399.25 kHz, and the ADC alone takes 120 of them.

The trick is that the ADC converts in the background, so we never wait for it. Each interrupt collects the result of the conversion started on the *previous* edge, then starts the next one and leaves. On every edge, in this order, the ISR:

1. Clears the interrupt flag.
2. Sends the sample it already has to the DAC.
3. Collects the ADC result from the last edge, and keeps it for next time.
4. Starts the next conversion. This is the moment we actually sample the input.
5. Checks whether anything went wrong.

The DAC write and ADC start happen first, and always at the same point after the edge. That matters for Stage 7, because however long the mixer takes, it won't change when we sample or when we output.

It costs us two sample periods of delay, because a sample is taken on one edge, collected on the next, and sent on the one after. That's 5 µs at the top rate and 40 µs at the slowest, which nobody will hear.

Both the ADC and DAC work with 12-bit numbers from 0 to 3.3 V, so passing a code straight across keeps the voltage the same. Whatever arrives at PA3 should appear at J1301.

## Setting it up

You need two signals: the clock, and some audio. The clock is the signal generator, set up just like Stage 4 (square wave, 0 to 3.3 V, on J1101 pin 6 with ground on pin 8). Start at **49.90625 kHz**.

The audio goes into the **J1302** jack. A second generator channel is ideal: a 2 kHz sine at 100 mVpp, with no offset. A phone playing a 2 kHz tone at low volume also works. We're using 2 kHz because it's close to the speaker's loudest point.

Before spending time here, we should sort out the Stage 2 volume problem. If audio isn't reaching the ADC, nothing will reach the speaker either.

Set `ACTIVE_TEST` to `6`, build with **F7**, load with **F8** and press **RESET**. The screen should say `ELEC4123 STAGE 5 PASSTHROUGH`. If you hear nothing, turn the R1303 knob slowly.

## What to check

**Can we hear it?** With the clock running and a tone going in, the tone should come out of the speaker. Stop the clock and the sound should stop too, which proves the external clock really is driving it.

**Does the wave look right?** Put a scope on J1301 with ground on J1303. You should see the tone as a stepped sine, centred on 1.65 V. With 100 mVpp going in, it should be about 1.1 Vpp, because the board's input amplifier multiplies by 11. The steps get finer as the clock gets faster. At fx/8 there are about 25 steps per wave of a 2 kHz tone, and at fx there are about 200.

Keep the input under about 260 mVpp here. The DAC can only swing from about 0.2 V to 3.1 V, so it clips a little sooner than the ADC does.

**Does it work at every rate?** Step the generator through all four:

| Generator | `CLOCK RATE (Hz)` | `BUDGET (cycles)` |
|---|---|---|
| 49.90625 kHz | About 49906 | 1442 |
| 99.8125 kHz | About 99812 | 721 |
| 199.625 kHz | About 199625 | 360 |
| 399.25 kHz | About 399250 | 180 |

`OVERRUNS` and `LATE EDGES` should stay at 0 while the clock is steady. Retuning the generator can throw out a stray short pulse, so a small jump right at that moment isn't a concern. If either keeps climbing, that's a real problem. LED5 should also keep flashing 60 times a minute.

**Are we losing edges?** Repeat the Stage 4 burst test here, this time with the real ISR. Send exactly 100,000 pulses at 399.25 kHz, and `TOTAL EDGES` should go up by exactly 100,000.

**How much headroom do we have?** Slowly raise the generator above 399.25 kHz and watch `OVERRUNS`. We expect it to start climbing around 450 kHz, but that's an estimate. Writing down the real number gives us our actual limit, and tells the clock hardware how far above 399.25 kHz it's safe to run. Don't bother going past about 600 kHz.

## What the numbers mean

| Screen line | Meaning | How it's worked out |
|---|---|---|
| `CLOCK RATE (Hz)` | How fast the clock is running | Edges in the last half second × 2 |
| `TOTAL EDGES` | Every edge since reset | The ISR adds 1 per edge |
| `OVERRUNS` | Edges where the last conversion hadn't finished | The ISR adds 1 if the ADC's done flag isn't set |
| `LATE EDGES` | Edges that arrived while the ISR was still busy | The ISR adds 1 if the pending flag is set again before it finishes |
| `ISR CYCLES` | Longest ISR in the last half second | Cycle counter at the end − at the start |
| `BUDGET (cycles)` | CPU cycles we get per sample | 72,000,000 ÷ `CLOCK RATE` |
| `ISR LOAD (%)` | How much of the budget the ISR uses | `ISR CYCLES` × 100 ÷ `BUDGET` |

An overrun means the sample is repeated, so we want that at 0 forever. A late edge is a warning that we're running out of time. The chip remembers one pending edge and handles it straight after, but if two arrive during one ISR, one of them is lost for good.

`ISR CYCLES` and `ISR LOAD` slightly undersell the real cost, because they can't see the CPU getting into and out of the interrupt, which takes about 30 cycles. So the honest load is (`ISR CYCLES` + 30) ÷ `BUDGET`. We'd guess `ISR CYCLES` will be around 50 to 80, which puts us at about 45 to 60% of the budget at the top rate. That's only a guess until we read it off the screen, and the spec wants the real number, so it goes in the journal.

You can double check it on a scope, because PF8 is high while the ISR runs. The PF8 pulse width × 72 MHz should roughly match `ISR CYCLES`.

## How the code works

The new code is in `audio.c`. `audio_init()` sets up the ADC and DAC, runs one conversion so the first edge has something to collect, and starts the external clock last, so the ISR can't fire before everything's ready.

The ISR moved here from `extclk.c`, since it's doing audio now. The core of it is these lines:

```c
dac_write(next_output);

if (adc_is_done()) {
	next_output = adc_result();
} else {
	audio_overruns++;
}

adc_start();
```

If the ADC hasn't finished, we don't wait. We count it and carry on, and the DAC repeats the last sample. Waiting would make us late for the next edge too.

`next_output` isn't `volatile`, because only the ISR touches it. The counters are, because the main loop reads them while the ISR changes them.

The main loop has almost nothing to do now. Audio runs by itself in the ISR, and `test_audio()` only works out the numbers and prints them twice a second. That's the shape we want for the final firmware, with the touchscreen sitting in the main loop and audio carrying on underneath it.

The Stage 4 test (`ACTIVE_TEST` 7) uses this same ISR, so it passes audio too. So does the full system at setting 4, where the touchscreen runs on top of it.

## If something's wrong

| Problem | Likely cause |
|---|---|
| No sound and J1301 sits at about 1.65 V | Audio isn't reaching the ADC, or the source volume is too low. Go back to the Stage 2 test |
| No sound and J1301 sits at 0 V, or it doesn't change | The clock isn't reaching PF11. Check `CLOCK RATE` isn't 0 |
| Wave on J1301 looks flat-topped | The input is too loud. Turn it down |
| `OVERRUNS` climbs at 399.25 kHz | Check `-O3` is still on, because the ISR is too slow without it |
| `LATE EDGES` climbs | The ISR is taking too long, or the clock has ringing that looks like extra edges |
| `CLOCK RATE` is about double | Ringing on the clock wire |
| LED5 slows down at high rates | The ISR is starving the main loop. Check `ISR LOAD` |
| Keil says `audio_init` is undefined | `audio.c` isn't in the project |

## Where this stands

The code compiles cleanly at all four test settings. We also ran the ISR on a computer with a simulated ADC and DAC. A ramp went in and came out unchanged two samples later, with nothing dropped or repeated, and a deliberately slow ADC showed up in `OVERRUNS`. Two deliberately broken versions were caught as well.

That checks the logic, not the timing or the sound, and **none of this has run on the board yet**. The spec also wants more than this before we can claim the audio path is finished. We still need the no-dropped-samples evidence (Stage 8), and aliasing and reconstruction are still unaddressed.

The design came from the spec's audio path requirements, RM0008 sections 11.3 and 11.6 (ADC) and 12.3 (DAC), and datasheet Tables 59 and 63.
