# Stage 1 Explained

## What we were trying to do

The chip starts at a slow 8 MHz, and at that speed we'd only get about 20 cycles per sample at 399.25 kHz. So before anything else, we had to switch it to 72 MHz using the crystal on the board, then find a way to prove the speed was really right.

There's no audio in this stage. We started here because every later stage depends on these clocks.

| File | What we use it for |
|---|---|
| `Sources/clock.h` | Names for each clock speed, including the crystal value `HSE_HZ` |
| `Sources/SystemInit.c` | Switches the chip to 72 MHz |
| `Sources/debug.c`, `debug.h` | Controls the LEDs, the scope pin and a cycle counter |
| `Sources/main.c` | Starts the board and picks which test runs |

## What we connect to the board

Stage 1 needs two cables, plus a scope if we have one handy.

| Connection | Where on the board | Why |
|---|---|---|
| USB-C cable | USB-C port (U301) | Powers the board. This is the only power source allowed |
| ST-Link V2 programmer | 20-pin JTAG connector J701 | Loads the code. The other end goes to the computer by USB |
| Scope probe (optional) | J1101 pin 3 (PF8) | Reads our test signal |
| Scope ground clip (optional) | J1101 pin 2 or 8 (GND) | Reference for the probe |

The USB-C port can't load code, which surprised us at first. Its data lines go to the microcontroller, not to a programmer, so we need the separate ST-Link.

### Before powering on

- Set the BOOT0 jumper J502 to the GND side, so the chip starts from Flash.
- Line up pin 1 on the J701 ribbon cable before pushing it in.
- Check that nothing is connected to the 3V3 and GND header pins except what's listed above.

### Power-on order

1. Connect the ST-Link to J701 and to the computer.
2. Connect the USB-C cable.
3. Slide the power switch SW301 on. The red LED D301 shows USB power is present, and the green LED D401 shows 3.3 V is on.
4. Flash the code.
5. Press RESET if the code doesn't start by itself.
6. Connect the scope ground to J1101 pin 2 before touching the probe to pin 3.

J1101 is pin 1 3V3, pin 2 GND, pin 3 PF8. We have to be careful not to bridge pins 1 and 2 with the probe, because that shorts 3.3 V to ground and can blow the fuse.

### What later stages add

| Stage | Add | Where |
|---|---|---|
| 2 and 5 | Audio source, 300 mVpp or less | J1302 audio jack |
| 3 and 5 | Scope on the DAC output | J1301, with ground on J1303 |
| 4 and 5 | Signal generator as a stand-in clock, 0 to 3.3 V square wave | J1101 pin 6 (PF11), with ground on pin 8 |
| 6 | Clock board, when built | J901 |

## What happens at power-on

Before writing anything, we had to work out what the chip does by itself:

1. The CPU reads two values from the start of Flash: the stack address and the address of `Reset_Handler`.
2. `Reset_Handler` in the startup file calls `SystemInit()`.
3. `__main` from the C library sets up global variables.
4. `main()` runs.

The order turned out to matter. `SystemInit()` runs before step 3, so anything it stores in a global variable gets overwritten. That's why `SystemInit()` only sets hardware registers, and `main()` reads the result back from them.

## Working through SystemInit.c

The clock has to change in a strict order, because a wrong order can crash the chip. Here's how we worked through it.

### 1. Slow down Flash first

```c
FLASH->ACR = FLASH_ACR_PRFTBE | (2U << FLASH_ACR_LATENCY_Pos);
```

Flash memory can't keep up with a 72 MHz CPU, so the CPU has to wait 2 extra cycles on each read. If we sped up first, it would read garbage instructions and crash. `FLASH_ACR_PRFTBE` keeps the prefetch buffer on, which reduces the cost of those waits.

We nearly used the header's `FLASH_ACR_LATENCY_2`, which looks right but isn't. It means bit 2 (value 4), not 2 wait states, so we shift `2U` into place instead.

### 2. Get to a known starting state

```c
RCC->CR |= RCC_CR_HSION;
wait_for(&RCC->CR, RCC_CR_HSIRDY, RCC_CR_HSIRDY);
RCC->CFGR &= ~RCC_CFGR_SW;
wait_for(&RCC->CFGR, RCC_CFGR_SWS, RCC_CFGR_SWS_HSI);
RCC->CR &= ~RCC_CR_PLLON;
wait_for(&RCC->CR, RCC_CR_PLLRDY, 0U);
```

This selects the internal 8 MHz clock and turns the PLL off. After a normal power-on that's already the state, but a debugger reset can land here with the PLL still running, and PLL settings can only change while it's off.

### 3. Start the crystal

```c
RCC->CR |= RCC_CR_HSEON;
if (!wait_for(&RCC->CR, RCC_CR_HSERDY, RCC_CR_HSERDY)) {
	return;
}
```

A crystal takes time to start, typically about 2 ms. If it never starts, we give up and stay at 8 MHz, so the board keeps running and `main()` can report the fault.

`wait_for()` checks a register repeatedly and gives up after a fixed number of tries, so a hardware fault can't hang the board forever.

### 4. Set the speed of each bus

```c
RCC->CFGR = RCC_CFGR_HPRE_DIV1 | RCC_CFGR_PPRE2_DIV1 | RCC_CFGR_PPRE1_DIV2
          | RCC_CFGR_ADCPRE_DIV6 | RCC_CFGR_PLLSRC | PLL_SETTINGS;
```

| Setting | Result | Why |
|---|---|---|
| `HPRE_DIV1` | CPU and main bus at 72 MHz | Full speed |
| `PPRE2_DIV1` | APB2 bus at 72 MHz | Runs GPIO, EXTI, ADC and SPI1 |
| `PPRE1_DIV2` | APB1 bus at 36 MHz | 36 MHz is this bus's limit. The DAC sits here |
| `ADCPRE_DIV6` | ADC clock at 12 MHz | The limit is 14 MHz, and /4 would give 18 MHz |
| `PLLSRC` | PLL takes its input from the crystal | Accurate, and can reach 72 MHz |
| `PLL_SETTINGS` | Multiplies up to 72 MHz | 12 MHz × 6, or 16 MHz ÷ 2 × 9 |

We set these while still at 8 MHz, so no bus ever runs too fast.

### 5. Start the PLL, then switch to it

```c
RCC->CR |= RCC_CR_PLLON;
if (!wait_for(&RCC->CR, RCC_CR_PLLRDY, RCC_CR_PLLRDY)) {
	return;
}

RCC->CFGR |= RCC_CFGR_SW_PLL;
wait_for(&RCC->CFGR, RCC_CFGR_SWS, RCC_CFGR_SWS_PLL);
```

The PLL is the circuit that multiplies the crystal frequency. It has to lock first, and then we can switch to it.

`SW` is our request and `SWS` is the hardware's confirmation, so we wait for `SWS` to change.

## SystemCoreClockUpdate()

We also wanted the firmware to report its own speed. This function works it out from what the registers say:

- It checks which clock is selected.
- If that's the PLL, it multiplies the PLL input by the multiplier.
- It stores the answer in `SystemCoreClock`.

The catch is that it trusts the value of `HSE_HZ` in `clock.h`. If the crystal is really 16 MHz, it still reports 72 MHz, so we needed a separate test that measures the real speed.

## debug.c

We knew we'd need ways to see inside the chip, so `dbg_init()` sets up three pins and a counter:

| Item | Setup | Purpose |
|---|---|---|
| PF0 (LED0, green) | Output | Heartbeat blink |
| PF7 (LED7, red) | Output | Lights on a clock fault |
| PF8 (J1101 pin 3) | Fast output | Scope test signal |
| DWT cycle counter | Turned on | Counts every CPU cycle |

Pins change through `BSRR`, a register that sets or clears a pin in a single write. That way, code in the main loop can't accidentally undo a pin change made by an interrupt.

## The original Stage 1 test

Our first test was a loop in `main.c`:

```c
while ((dbg_cycles() - last) < HALF_PERIOD_CYCLES) {
}
last += HALF_PERIOD_CYCLES;
dbg_scope_toggle();
```

`HALF_PERIOD_CYCLES` was a fixed 36,000 cycles:

- At 72 MHz, 36,000 cycles is 0.5 ms. Toggling every 0.5 ms made a **1 kHz** square wave on PF8.
- After every 1,000 toggles, or 0.5 s, LED0 toggled. That's **one flash per second**.

**That loop has since gone.** Stages 2 onwards replaced it, so PF8 no longer gives 1 kHz. To run it again, we can look at commit `4fccd1a`. The LED check survives though: the Stage 2 and Stage 4 tests still flash LED0 once a second at 72 MHz. The Stage 3 test doesn't right now, because we commented out its LED toggle during the beep while debugging.

`main.c` now just starts the board and runs whichever test `ACTIVE_TEST` picks. It loops forever, because returning from `main()` would end in a breakpoint instruction that faults when no debugger is attached.

## How we know it works

### Why the test measures real speed

The firmware always waits a fixed number of cycles, whatever speed it's really running at. So if the chip is faster or slower, the output changes by the same ratio. A scope or stopwatch is an independent check, because neither relies on the chip's own idea of its speed.

| PF8 frequency (original test) | LED0 flashes per minute | CPU speed | Meaning |
|---|---|---|---|
| **1.000 kHz** | **60** | **72 MHz** | **Correct, 12 MHz crystal** |
| 1.333 kHz | 80 | 96 MHz | Crystal is 16 MHz. Change `HSE_HZ`, rebuild straight away |
| 111 Hz | About 7 | 8 MHz | The speed-up failed |

### What the registers should say

The original Stage 1 screen also showed two register values:

| LCD line | Expected | What it proves |
|---|---|---|
| `SYSCLK (Hz)` | `72000000` | The registers say 72 MHz. This doesn't prove the real speed |
| `RCC_CFGR (hex)` | `11840A` | Clock source, multiplier and bus dividers are all set as intended |
| `FLASH_ACR (hex)` | `32` | 2 wait states, prefetch on and active |

`11840A` breaks down like this:

| Bits | Value | Meaning |
|---|---|---|
| `SW`, `SWS` | `0xA` | PLL requested and confirmed running |
| `PPRE1` | `0x400` | APB1 divided by 2 |
| `ADCPRE` | `0x8000` | ADC divided by 6 |
| `PLLSRC` | `0x10000` | PLL input is the crystal |
| `PLLMUL` | `0x100000` | Multiply by 6 |

The LCD working at all is useful evidence too, because the Design Guide warns that the screen and code stop working at 72 MHz if the Flash setting is wrong.

### What we've checked so far

| Check | Result | What it doesn't prove |
|---|---|---|
| Compiled for Cortex-M3 with Clang, C90 style | No warnings in the new code, all symbols resolve | Only a compile check |
| Simulated `SystemInit()` against a software model of the clock rules | No violations for cold boot, debugger re-entry, dead crystal or PLL failure, with 12 and 16 MHz settings | The model is our own reading of the manual, not real silicon |
| Broke the code on purpose | The model caught the bad Flash setting and the PLL being changed while on | Only shows that the model catches those faults |
| Built and loaded in Keil 5 | The later test screens have run on the board | Doesn't prove the speed is exactly 72 MHz |
| Counted LED0 for a minute | **Still to do** | |

Stage 1 only counts as finished once we've counted 60 flashes a minute, which also settles the crystal question.

### Where the numbers come from

| Fact | Source |
|---|---|
| 2 wait states above 48 MHz | RM0008 section 3.3.3 |
| Clock register layouts | RM0008 pages 99 to 103 |
| PLL settings need the PLL off | RM0008 `RCC_CFGR` description |
| APB1 limit 36 MHz, ADC limit 14 MHz | RM0008 section 7.2, Figure 8 |
| PLL lock up to 200 µs, maximum 72 MHz | Datasheet Table 28 |
| Crystal start-up typically 2 ms | Datasheet Table 23 |
| LED and pin wiring | APE schematic sheets 10 and 11 |
| 12 MHz crystal | Board Guide Ch 2 and schematic symbol. The schematic's part number says 16 MHz, which is why we test it |
