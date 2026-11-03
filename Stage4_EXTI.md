# Stage 4: Listening to the External Clock

## What we're trying to do

So far the chip has been doing everything on its own schedule. The spec doesn't allow that for audio. Sampling has to be driven by our external clock, so the first thing we need is for the chip to notice every rising edge on a pin, instantly, whatever else it's busy doing.

That's what an interrupt is for. In this stage, each rising edge on PF11 makes the chip drop what it's doing, run a tiny function, and go back. For now that function only counts the edge and pulses a pin so we can see it happen. Real audio goes in there in Stage 5.

We can test all of this before the clock board exists, because the spec lets us use a signal generator for testing. It just can't be part of the final system.

## How an edge reaches our code

It helped us to follow one edge through the chip:

```text
PF11 pin -> AFIO routing -> EXTI edge detector -> NVIC -> vector table -> EXTI15_10_IRQHandler()
```

| Stop | What it does |
|---|---|
| **PF11 pin** | Where the clock wire plugs in |
| **AFIO** | Every port has a pin 11, so this picks port F for line 11 |
| **EXTI** | Watches for a rising edge and raises a pending flag |
| **NVIC** | The CPU's interrupt manager. It decides what runs and when |
| **Vector table** | The list of function addresses in the startup file |
| **Our handler** | The function that runs on each edge |

## What we need

- Everything from Stage 1: USB-C cable and ST-Link
- A signal generator, as our stand-in clock
- A scope is nice to have, but the screen alone can prove this works

## Setting up the signal generator

We need to be careful here, because this wire goes straight into a chip pin. It has to stay between 0 and 3.3 V, and the chip's absolute limits are −0.3 V and 4.0 V.

| Setting | Value |
|---|---|
| Waveform | Square, 50% duty |
| Low level | 0 V |
| High level | 3.3 V |
| Output load | High-Z, so the displayed voltage is correct |
| Frequency | 1 kHz to start |

Then we connect it in this order:

1. Check the output on a scope first, with the board disconnected.
2. Turn the generator output off, and power up the board.
3. Connect generator ground to **J1101 pin 8**.
4. Connect the signal to **J1101 pin 6** (PF11). A 1 kΩ resistor in series is cheap extra protection.
5. Turn the generator output on.

When we're done, we turn the signal off before powering down the board. A driven pin on an unpowered chip isn't good for it.

## Running it

1. Connect the board the same way as Stage 1.
2. Check `ACTIVE_TEST` at the top of `main.c` is `4`.
3. In Keil, check `extclk.c` and `extclk.h` are listed under Sources. They should already be in `empty_skeleton.uvprojx`.
4. Press **F7** to build, then **F8** to load.
5. Press **RESET** on the board.

The screen should say `ELEC4123 STAGE 4 CLOCK TEST`.

## What we expect to see

### Test 1: nothing connected

`CLOCK RATE (Hz)` should sit at 0. That proves the pin is quiet by itself and isn't firing random interrupts.

### Test 2: step through our four rates

We start at 1 kHz, then set the generator to each of our real rates:

| Generator | `CLOCK RATE (Hz)` should read |
|---|---|
| 1 kHz | 1000 |
| 49.90625 kHz | About 49906 |
| 99.8125 kHz | About 99812 |
| 199.625 kHz | About 199625 |
| 399.25 kHz | About 399250 |

The screen rate is always an even number, and a difference of a few Hz is normal. The generator and our crystal are never perfectly matched. If it reads double or half, something is wrong (see the table further down).

### Test 3: count an exact number of edges

This is the test we like best, because it's proper evidence that we aren't missing interrupts. If the generator has a burst mode:

1. Set it to 399.25 kHz and a burst of exactly 100,000 pulses.
2. Write down `TOTAL EDGES`.
3. Send one burst.
4. `TOTAL EDGES` should go up by **exactly** 100,000.

If it's lower, we lost interrupts. If it's higher, the signal has ringing and we're counting extra edges.

### Test 4: watch it on the scope (optional)

| Setting | Value |
|---|---|
| Channel 1 | PF11 (J1101 pin 6) |
| Channel 2 | PF8 (J1101 pin 3) |
| Trigger | Channel 1, rising edge |
| Horizontal | 200 ns/div |

PF8 pulses once for every clock edge, so it runs at the same frequency as the clock.

| What to read | Why we care | Expected |
|---|---|---|
| Delay from PF11 rising to PF8 rising | How long the chip takes to react | Roughly 0.25 to 0.4 µs (estimate) |
| PF8 pulse width | How long our ISR runs | Roughly 0.1 to 0.2 µs (estimate) |
| PF8 frequency | One interrupt per edge | Same as the generator |

These two timings are our first real measurement of the 180-cycle budget at 399.25 kHz, so they're worth saving for the journal.

### Passed when

- Nothing connected reads 0
- All four rates read correctly
- LED0 still flashes 60 times a minute, even at 399.25 kHz

That last one matters. LED0 is run by the main loop, so if it keeps its timing, the interrupts aren't starving the rest of the program.

## What the numbers mean

| Screen line | What it means | How it's calculated |
|---|---|---|
| `SYSCLK (Hz)` | CPU speed the registers report | Read from the clock settings |
| `CLOCK RATE (Hz)` | How fast the external clock is running | Edges in the last half second × 2 |
| `TOTAL EDGES` | Every edge counted since reset | The ISR adds 1 per edge |

We get the half second from the CPU's cycle counter: 36,000,000 cycles at 72 MHz. So at 399.25 kHz, we'd count 199,625 edges in a half second, and 199,625 × 2 = 399,250.

Doubling means we can only ever show even numbers, so the rate is accurate to about 2 Hz. That's plenty for now.

The spec says we can use the chip's timing to *measure* the rate, as long as sampling itself isn't timed from it. We'll use this later too, because the filters and FFT labels need the real rate rather than the nominal one.

## How the code works

### extclk_init() in extclk.c

| Step | Code | Why |
|---|---|---|
| 1 | `RCC->APB2ENR \|= IOPFEN \| AFIOEN` | Turns on clocks for port F and the routing block |
| 2 | `GPIOF->BSRR = BR11` | Selects pull-down, not pull-up, for this pin |
| 3 | `GPIOF->CRH` CNF11 = 10, MODE11 = 00 | Makes PF11 an input with a pull resistor |
| 4 | `AFIO->EXTICR[2]`, EXTI11 = 0101 | Connects PF11 to EXTI line 11 |
| 5 | `EXTI->RTSR \|= TR11` | Rising edges trigger it |
| 6 | `EXTI->FTSR &= ~TR11` | Falling edges don't |
| 7 | `EXTI->PR = PR11` | Clears anything left over from setup |
| 8 | `EXTI->IMR \|= MR11` | Lets line 11 through |
| 9 | `NVIC_ClearPendingIRQ(...)` | Clears anything already waiting in the NVIC |
| 10 | `NVIC_SetPriority(..., 0)` | Priority 0 is the highest |
| 11 | `NVIC_EnableIRQ(...)` | Turns on the interrupt |

A few of these caught us out when we read the manuals:

- **`EXTICR[2]` is really `AFIO_EXTICR3`.** The array starts at 0, but the manual counts from 1.
- **Line 11 shares its interrupt.** Lines 10 to 15 all use `EXTI15_10_IRQHandler`. We only unmask line 11, so we don't need to check which one fired, but if a teammate enables another line we will.
- **The Board Guide's NVIC line is wrong.** It says `NVIC->ISER[k/32] = (k%32)`, but it needs to be `1 << (k%32)`. We use `NVIC_EnableIRQ`, which gets it right.
- **We chose pull-down, not the guide's pull-up.** Our clock is a driven signal, so either works, but pull-down keeps an unplugged pin low.

### The interrupt handler

```c
void EXTI15_10_IRQHandler(void) {
	dbg_scope_high();
	EXTI->PR = EXTI_PR_PR11;
	extclk_edges++;
	dbg_scope_low();
}
```

**The name has to be exact.** The startup file has a weak default with this name, and ours replaces it when linking. If we misspell it, the program still builds, but the default handler runs instead and hangs forever.

**Clearing `PR` is the important line.** It's a strange register: we write a 1 to clear the flag. If we forget, the moment the handler ends it fires again, and the main loop never runs.

**`extclk_edges` is `volatile`** because the ISR changes it behind the main loop's back. Without that, the compiler could keep an old copy.

### Why we can read the counter safely

A 32-bit read on this chip happens in one go, so the ISR can't interrupt it halfway. And only the ISR writes the counter. That means the main loop can read it without turning interrupts off, which is good, because turning them off could delay a sample.

## If something's wrong

| Problem | Likely cause |
|---|---|
| Rate reads 0 with the generator on | Output is off, signal isn't on J1101 pin 6, or grounds aren't connected |
| Rate reads about half | The wire may be on the wrong pin, or the high level is too low. It should reach 3.3 V |
| Rate reads about double | Falling edges are being counted, or the signal has ringing. Shorten the wires and check on a scope |
| Rate jumps around | Loose wire or a bad ground. Keep the ground wire right beside the signal wire |
| Screen stops at `SYSCLK` and LED0 stops | The ISR is stuck firing. Check `EXTI->PR` is still cleared |
| Keil says `extclk_init` or `test_extclk` is undefined | `extclk.c` isn't in the project. Add it under Sources |
| Screen still says STAGE 3 | `ACTIVE_TEST` isn't `4`, or the new code didn't load |
| LED0 flashes 80 times a minute | Crystal is 16 MHz. Change `HSE_HZ` in `clock.h` to `16000000U` |

## How we know it works

| Claim | How we know |
|---|---|
| PF11 is free and on J1101 pin 6 | Schematic sheets 5 and 11 |
| Port F value for EXTICR is 0101 | RM0008 section 9.4, page 192 |
| Line 11 uses the EXTI15_10 interrupt, number 40 | RM0008 Table 63, `stm32f103xe.h` |
| Pending bit clears by writing 1 | RM0008 section 10.3.6 |
| Handler name | `startup_stm32f10x_hd.s` |
| Minimum edge width of 10 ns | Datasheet Table 48 |
| Signal generator is allowed for testing | Task spec, sampling clock conditions |

**Checked so far:**

- All code compiles for Cortex-M3 with Clang with no warnings in our files, at all three `ACTIVE_TEST` settings.
- `extclk_init()` ran on a computer with fake registers, and the register values matched the plan.
- The handler ran five times on fake registers, and it counted 5 and cleared the flag each time.

**Not done yet:** this stage hasn't been built in Keil or run on the board. Fake registers can't show real timing, so only the generator test proves this works.
