# Stage 1: getting to 72 MHz

The chip boots at 8 MHz. At our top sample rate of 399.25 kHz, that leaves about 20 CPU cycles per sample, which is nothing. At 72 MHz we get about 180, so that's where everything starts. Nothing in this stage touches audio, but every later stage depends on these clocks being right.

The work is spread across a few files. `clock.h` names the clock speeds, `SystemInit.c` does the actual switch, `debug.c` gives us LEDs, a scope pin and a cycle counter, and `main.c` starts the board and picks a test to run.

## Hooking up the board

The board needs two cables: USB-C for power, and an ST-Link V2 on the 20-pin connector J701 to load code. USB-C can't program the board, because its data lines only go to the microcontroller's USB pins.

Before powering on, make sure jumper J502 is on the GND side so the chip boots from Flash, and line up pin 1 of the ribbon cable before pushing it in. Then connect the ST-Link, plug in USB-C and slide SW301 on. The red LED D301 means USB power is there, and the green LED D401 means 3.3 V is up.

A scope is optional. If you use one, the probe goes on J1101 pin 3 (PF8) and the ground clip on pin 2 or 8. Be careful around pins 1 and 2: pin 1 is 3.3 V and pin 2 is ground, so bridging them can blow the fuse.

## What the chip does at power-on

The CPU reads a stack address and the address of `Reset_Handler` from the start of Flash. `Reset_Handler` calls our `SystemInit()`, then `__main` sets up global variables, and only then does `main()` run.

That order matters, because it means anything `SystemInit()` puts in a global gets wiped. So `SystemInit()` only touches hardware, and `main()` reads the result back from the registers.

## Switching the clock

Order matters here, because doing this wrong can crash the chip.

**Flash goes first.** Flash can't keep up with a 72 MHz CPU, so the CPU has to wait 2 extra cycles on each read. If we sped up before setting that, the CPU would start reading garbage instructions.

```c
FLASH->ACR = FLASH_ACR_PRFTBE | (2U << FLASH_ACR_LATENCY_Pos);
```

Watch out for the header's `FLASH_ACR_LATENCY_2` here. It looks right, but it means bit 2 (value 4), not 2 wait states.

**Then we get to a known state.** We select the internal clock and stop the PLL. After a normal power-on that's already how things are, but a debugger reset can land here with the PLL still running, and its settings only change while it's off.

**Then we start the crystal.** It takes about 2 ms. If it never starts, `wait_for()` times out, we stay at 8 MHz, and `main()` reports the fault. The timeout means a dead crystal can't hang the board.

**Next come the bus speeds**, set while we're still at 8 MHz so nothing ever runs too fast:

```c
RCC->CFGR = RCC_CFGR_HPRE_DIV1 | RCC_CFGR_PPRE2_DIV1 | RCC_CFGR_PPRE1_DIV2
          | RCC_CFGR_ADCPRE_DIV6 | RCC_CFGR_PLLSRC | PLL_SETTINGS;
```

| Bus | Speed | Why |
|---|---|---|
| CPU and main bus | 72 MHz | Full speed |
| APB2 (GPIO, EXTI, ADC, SPI1) | 72 MHz | Can run flat out |
| APB1 (DAC) | 36 MHz | 36 MHz is its limit |
| ADC clock | 12 MHz | Limit is 14 MHz, and dividing by 4 would give 18 |

The PLL takes the crystal and multiplies it: 12 MHz × 6, or 16 MHz ÷ 2 × 9 if the crystal turns out to be 16 MHz.

**Finally we start the PLL**, wait for it to lock, and switch over. `SW` is our request and `SWS` is the hardware confirming it, so we wait for `SWS`.

## Knowing the speed

`SystemCoreClockUpdate()` reads the registers and works out the CPU speed. There's a catch: it trusts `HSE_HZ`. If the crystal is really 16 MHz, it'll still happily report 72 MHz. That's why we needed a test that measures the speed from outside the chip.

## The debug tools

`dbg_init()` sets up LED5 (PF5) as a heartbeat, LED7 (PF7) as a clock fault light, PF8 as a fast scope pin, and the CPU's cycle counter. Pins change through `BSRR`, which sets or clears a pin in one write, so the main loop can't accidentally undo a pin change made by an interrupt.

## The test, and why it works

The trick is that the firmware counts a fixed number of CPU cycles, whatever speed it's really running at. If the chip runs fast or slow, everything timed that way runs fast or slow by the same ratio, and a stopwatch can see that even though the chip can't.

Our first test toggled PF8 every 36,000 cycles, which is 0.5 ms at 72 MHz, giving a 1 kHz square wave. Later stages replaced that loop, so PF8 no longer does this (it's in commit `4fccd1a` if we want it back). The LED check survived, though. The ADC test (`ACTIVE_TEST` 2), the audio stats test (6) and the clock count test (7) flash LED5 once a second at 72 MHz. It used to be LED0, until the rate LEDs took PF0 to PF4:

| LED5 flashes per minute | CPU speed | Meaning |
|---|---|---|
| 60 | 72 MHz | Correct, 12 MHz crystal |
| 80 | 96 MHz | Crystal is 16 MHz. Change `HSE_HZ` and rebuild straight away |
| About 7 | 8 MHz | The speed-up failed |

The Stage 3 test doesn't work for this right now, because the LED toggle during its beep is commented out for debugging.

The original screen also showed two registers, which are handy if we ever need to check them in the debugger. `RCC_CFGR` should read `11840A`: that's `0xA` for PLL requested and running, `0x400` for APB1 ÷ 2, `0x8000` for ADC ÷ 6, `0x10000` for the crystal as PLL input, and `0x100000` for × 6. `FLASH_ACR` should read `32`, meaning 2 wait states with prefetch on.

The screen working at all is decent evidence too, because the Design Guide warns that the screen and code stop working at 72 MHz if the Flash setting is wrong.

## Where this stands

Before we had a board in front of us, we compiled everything with Clang and ran `SystemInit()` against a software model of the clock rules, covering cold boot, debugger re-entry, a dead crystal and a PLL that won't lock. There were no violations, and the model caught it when we broke the code on purpose. That model is only our reading of the manual, though, so it doesn't prove anything about real silicon.

The code has since built in Keil 5 and run on the board. **We still need to count LED5 for a minute**, which also settles the crystal question. The Board Guide and schematic symbol say 12 MHz, but the schematic's part number says 16 MHz.

The numbers came from RM0008 (section 3.3.3 for Flash, pages 99 to 103 for the clock registers, Figure 8 for the bus limits), datasheet Tables 23 and 28 (crystal start-up and PLL lock), and schematic sheets 10 and 11 (LEDs and pins).
