# Stage 4: listening to the external clock

Up to now the chip has been running on its own schedule, and the spec doesn't allow that for audio. Sampling has to be driven by our external clock. So we need the chip to notice every rising edge on a pin, right away, whatever else it's doing.

That's what an interrupt is for. Each rising edge on PF11 makes the chip drop what it's doing, run a short function, and carry on. This test watches that happen by counting edges and pulsing a pin. Since Stage 5, the same function also does the audio, so this test passes sound through too.

The clock board doesn't need to exist yet, because the spec lets us test with a signal generator. It just can't be part of the final system.

## How an edge gets to our code

It helps to follow one edge through the chip:

```text
PF11 -> AFIO -> EXTI -> NVIC -> vector table -> EXTI15_10_IRQHandler()
```

Every port has a pin 11, so AFIO picks port F for line 11. EXTI watches that line for a rising edge and raises a pending flag. The NVIC is the CPU's interrupt manager, and it looks up our function's address in the vector table and runs it.

## Connecting the signal generator

This wire goes straight into a chip pin, so it has to stay between 0 and 3.3 V. The absolute limits are −0.3 V and 4.0 V.

Set the generator to a square wave at 1 kHz, 50% duty, high level 3.3 V, low level 0 V, output load High-Z. Check it on a scope with the board disconnected first. If it shows 6.6 V or goes negative, fix the settings before going any further.

J1101 is the 8-pin header next to the breadboard:

| Pin | Signal |
|---|---|
| 1 | 3.3 V |
| 2 | GND |
| 3 | PF8 (scope pin) |
| 4 | PF9 |
| 5 | PF10 |
| **6** | **PF11, the generator signal** |
| 7 | 3.3 V |
| **8** | **GND, the generator ground** |

Pin 7 is 3.3 V and sits right between our two wires, so it's worth finding pin 1 with a multimeter first. With the board on and the black probe on J1303, pin 1 reads 3.3 V and pin 8 reads 0 V.

Turn the generator output off and power up the board. Connect a BNC-to-crocodile-clip lead through two jumper wires, black to pin 8 first, then red to pin 6, and turn the output on. A 1 kΩ resistor in series on the breadboard is cheap extra protection.

The black clip must only ever go to GND, because generators and scopes are usually earthed. When you're done, turn the signal off before powering down the board.

## Running it

Set `ACTIVE_TEST` to `7`, build with **F7**, load with **F8** and press **RESET**. The screen should say `ELEC4123 STAGE 4 CLOCK TEST`.

With nothing connected, `CLOCK RATE (Hz)` should sit at 0, which shows the pin isn't firing random interrupts. Then start the generator at 1 kHz and step up through our four rates:

| Generator | `CLOCK RATE (Hz)` |
|---|---|
| 1 kHz | 1000 |
| 49.90625 kHz | About 49906 |
| 99.8125 kHz | About 99812 |
| 199.625 kHz | About 199625 |
| 399.25 kHz | About 399250 |

A few Hz either way is normal, because the generator and our crystal never match perfectly. Watch LED5 at 399.25 kHz too. It should still flash 60 times a minute. The main loop drives that LED, so if it keeps time, the interrupts aren't starving the rest of the program.

## The best test

If the generator has a burst mode, set it to 399.25 kHz and a burst of exactly 100,000 pulses. Write down `TOTAL EDGES`, send one burst, and it should go up by exactly 100,000. Any lower and we lost interrupts. Any higher and the signal is ringing, so we're counting extra edges. This is real evidence for the marker that we aren't missing anything.

With a scope, put channel 1 on PF11 and channel 2 on PF8, triggered on PF11's rising edge at about 200 ns/div. PF8 pulses once per clock edge. The delay before it rises shows how long the chip takes to react, and we'd guess about 0.25 to 0.4 µs. The pulse width shows how long our ISR runs, which should be under a microsecond now that it includes the audio work. Those are estimates, and these two numbers will be our first real measurement of the 180-cycle budget, so save them for the journal.

## What the numbers mean

The window is half a second. The code waits 36,000,000 CPU cycles, counts how many edges arrived, doubles that for `CLOCK RATE (Hz)`, and goes straight into the next window. At 399.25 kHz that's 199,625 edges, and 199,625 × 2 = 399,250. Because of the doubling, it only shows even numbers, so it moves in 2 Hz steps.

`TOTAL EDGES` ignores the window and counts every edge since reset.

The spec lets us use the chip's timing to measure the rate, as long as sampling isn't timed from it. We'll need that later, because the filters and FFT labels need the real rate, not the nominal one.

## How the code works

`extclk_init()` turns on the clocks for port F and AFIO, and makes PF11 an input with a pull-down, so an unplugged wire sits low. It connects PF11 to line 11, picks rising edges only, clears anything left over from setup, then unmasks the line and enables the interrupt at priority 0, the highest.

A few things in the manuals are easy to trip over. `EXTICR[2]` is what the manual calls `AFIO_EXTICR3`, because the array starts at 0 and the manual starts at 1. Lines 10 to 15 all share one handler, which is fine while only line 11 is enabled, but we'd have to check which line fired if a teammate added another. The Board Guide's `NVIC->ISER[k/32] = (k%32)` is also wrong and should be `1 << (k%32)`, so we use `NVIC_EnableIRQ`. And we went with a pull-down rather than the guide's pull-up, since our clock is driven anyway and this keeps an unplugged pin quiet.

The handler started out tiny, and moved to `audio.c` in Stage 5. These are the lines that came from this stage:

```c
void EXTI15_10_IRQHandler(void) {
	dbg_scope_high();
	EXTI->PR = EXTI_PR_PR11;
	extclk_edges++;
	/* the audio work goes here */
	dbg_scope_low();
}
```

The name has to be exact. The startup file has a weak default with this name, and ours replaces it when linking. Misspell it and the program still builds, but the default runs instead and hangs forever.

The `PR` line is the important one. We clear the flag by writing a 1, which is odd, but that's how this register works. Forget it, and the handler fires again the moment it ends, so the main loop never runs.

`extclk_edges` is `volatile` because the ISR changes it behind the main loop's back. The main loop can read it without turning interrupts off, because a 32-bit read happens in one go. That's a good thing, since turning interrupts off could delay a sample.

## If something's wrong

| Problem | Likely cause |
|---|---|
| Rate reads 0 with the generator on | Output is off, signal isn't on pin 6, or grounds aren't connected |
| Rate is about half | High level is too low, or the wire is on the wrong pin |
| Rate is about double | The signal is ringing. Shorten the wires and check on a scope |
| Rate jumps around | Loose wire or bad ground. Keep the ground wire beside the signal wire |
| Screen stops at `SYSCLK` and LED5 stops | The ISR is stuck firing. Check `EXTI->PR` is still cleared |
| Keil says `test_extclk` or `audio_init` is undefined | `tests.c` or `audio.c` isn't in the project |
| Keil says `core_cm3.h` not found | `.\Headers` is missing from the C/C++ include paths |

## Where this stands

The code compiles cleanly, and `extclk_init()` gave the right register values on simulated hardware, where the handler counted correctly too. Simulation can't show real timing, though, and **we haven't recorded any generator results yet**. That's what proves this stage.

The facts came from schematic sheets 5 and 11, RM0008 page 192 and sections 10.1.2 and 10.3.6, datasheet Table 48, and the task spec's sampling clock conditions.
