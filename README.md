# Embedded Audio Sampler: System Overview

ELEC4123 Embedded System Design elective, T3 2026. APE board (STM32F103ZCT6, 72 MHz).

This describes the firmware as it stands at commit `53ae9a8` with `ACTIVE_TEST 10` (full system with the 3-band mixer). It goes top-down: hardware, boot, the per-sample path, the main loop, then the files, design choices, key numbers and likely demo questions.

## 1. Big picture

```
Audio jack J1302 → MAX4466 amp (×11) → PA3 / ADC1 ch3
                                           │
74HC clock board ──PF11 rising edge──► EXTI11 ISR (priority 0)
   ▲                                       │  sample → Sampling → mixer (fx/8 only)
   │ SPI1 + CMD_LATCH + APPLY              ▼
   │                                  DAC ch1 / PA4 → J1301 → volume pot → MAX9700 → speaker
Main loop: touchscreen UI, rate control, stats  (everything slow lives here)
```

The design splits into two worlds:

- **The ISR** does all the audio. It is short and fixed-time, and it never waits.
- **The main loop** does everything slow: LCD drawing, touch, SPI commands to the clock board.

Audio can never drop out because of the UI: the ISR has the highest priority (0) and interrupts the main loop on every clock edge.

## 2. Boot sequence

1. **`Reset_Handler`** (startup file) calls **`SystemInit()`** (`SystemInit.c`) before the C runtime starts.
   - Sets flash to 2 wait states plus prefetch (`FLASH->ACR`). This is required above 48 MHz and must happen before speeding up.
   - Starts the 12 MHz crystal (`RCC->CR HSEON`), sets the PLL to ×6 for **72 MHz**, then switches `SW` to the PLL and confirms via `SWS`.
   - Bus dividers: AHB 72 MHz, APB2 72 MHz (GPIO, EXTI, ADC, SPI1), APB1 36 MHz (DAC; its limit is 36 MHz), ADC clock 72/6 = **12 MHz** (limit 14).
   - Every wait has a timeout, so a dead crystal leaves the chip on 8 MHz instead of hanging.
2. **`main()`**:
   - `SystemCoreClockUpdate()` reads the clock back. If it isn't 72 MHz, the red LED (PF7) comes on and "CLOCK FAULT" is printed.
   - `dbg_init()` sets up the heartbeat LED (PF5), the scope pin PF8 (50 MHz output for sharp edges) and the **DWT cycle counter** (`DWT->CYCCNT`, with `TRCENA` set).
   - `lcd_init()` sets up the FSMC and the ILI9341.
3. **`ACTIVE_TEST 10` branch**, in this order on purpose:
   - `audio_init()`: ADC, then DAC, then one dummy conversion so the first edge has a result to collect, then the external clock **last**, so the ISR can't fire before the rest is ready.
   - `sampling_init()`: plugs the Sampling function into the ISR (`audio_process` pointer).
   - `rate_init()`: rate LEDs, then sends **E0, D8, E1** over SPI. The board's flip-flops power up in random states, so it is forced off, set to fx/8, then turned on.
   - `ui_init()`: designs the mixer filters, turns the mixer on if the rate is fx/8, starts touch and push-buttons, and draws the main page.
4. **`while(1) ui_poll();`** runs forever.

## 3. Per-sample path: `EXTI15_10_IRQHandler` (`audio.c`)

Every rising edge on PF11 runs these steps:

| Step | Code | Register | Why |
|---|---|---|---|
| 1 | `EXTI->PR = EXTI_PR_PR11` | write 1 to clear | Left pending, the ISR would re-fire forever |
| 2 | `dac_write(next_output)` | `DAC->DHR12R1` | Output at a **fixed time** after the edge, with no jitter from processing |
| 3 | `adc_is_done()` / `adc_result()` | `ADC1->SR.EOC`, `ADC1->DR` | Collect the conversion started on the **previous** edge. If it isn't done, count an overrun and repeat the last sample, never wait |
| 4 | `adc_start()` | `ADC1->CR2 SWSTART` | **This is the sampling instant**, also at a fixed time after the edge |
| 5 | `process(code)` | `audio_process` pointer | Passthrough (0) or `sampling_process` (test 10) |
| 6 | check `EXTI->PR` again | | Set again means the next edge arrived during the ISR, so `audio_late_edges++` |
| 7 | `audio_max_cycles` | `DWT->CYCCNT` | Longest ISR, shown on screen as CPU load |

**Pipelined ADC.** A conversion takes (7.5 + 12.5) ADC clocks = 1.67 µs = **120 CPU cycles**, but at fx there are only **180 cycles** per sample. Waiting for it inside the ISR would use most of the budget. Collecting the previous result instead costs **2 samples of latency**: 5 µs at fx, 40 µs at fx/8. Nobody can hear that.

**Processing after the ADC start.** The mixer's run time can then never shift when we sample or output.

**PF8** is high for the whole ISR, so pulse width × 72 MHz on a scope cross-checks the cycle count.

### Sampling (`Sampling/sampling.c`) and mixer (`ui/mixer.c`)

`sampling_process(code)`:

1. **Rate check:** if the mixer is off (any rate faster than fx/8), return the code untouched. This keeps fx within its 180-cycle budget.
2. **Clip check:** codes below 16 or above 4079 increment `sampling_clips` (input too loud).
3. **Offset and DC removal:** `x = code − 2048`, minus a running average (`dc_sum`, time constant 4096 samples, about a 2 Hz high-pass). Without this, the low slider (×2) would also move the DC level and click.
4. **Mixer:** calls `mixer_process(x × 256)`, then adds 2048 back for the DAC.

`mixer_process()`:

- **8 biquads** in three chains: low = LP·LP at 500 Hz; mid = HP·HP at 500 Hz then LP·LP at 4 kHz; high = HP·HP at 4 kHz. Each pair is a Linkwitz-Riley LR4 filter.
- Then `g1·low + g2·mid + g3·high`, rounded, shifted down 22 bits, and saturated to ±2048.
- **Integers only:** Q2.30 coefficients, int64 accumulator (SMULL/SMLAL), 8 extra state bits, and error feedback, which stops the 500 Hz filter getting stuck at a DC level.
- It is bit-exact with `mixer_design.m`: 0 mismatches over 20 000 samples.

## 4. Main loop: `ui_poll()` (`ui/ui.c`)

Each pass does four things:

1. **`touch_poll()`** checks the pen every 10 ms.
2. **`stats_update()`** runs every 0.5 s:
   - Measures the real sample rate from `extclk_edges` and the elapsed DWT cycles.
   - Reads and resets `audio_max_cycles`.
   - If the mixer is on and the measured rate is more than 1 % from the filters' design rate, redesigns the coefficients. The clock is an RC oscillator, so fs isn't exactly 49 906 Hz.
3. **Push-buttons:** PB0 = next rate, PB1 = clock on/off, either = back on other pages. Polled every 10 ms with a 30 ms debounce.
4. **`ui_test_x`/`ui_test_y`**, a debugger hook to fake a touch.

**Touch (`touch.c`).** The touch controller is an XPT2046 on SPI1 at 1.125 MHz (72 MHz / 64), with chip select PC5 and pen-down signal PA0 (read by polling, not an interrupt).

- A press counts after the pen has been down 20 ms.
- It reads X (command `0xD0`) and Y (`0x90`) 8 times each, drops the highest and lowest, and averages the rest.
- It rejects the reading if the samples spread more than 100 counts.
- It maps raw readings to pixels using the calibration constants in `touch.h`.
- One press gives one event; the pen must lift for 30 ms before the next counts.

**Pages:**

- **Main page:** rate status box (including `LAST CMD 0x..`), four rate buttons, CLKON/CLKOFF, D3 BAD, MIXER, SPECTRUM.
- **Mixer page:** three tap-to-set gain bars (0–2 in steps of 0.05), the fixed 500 Hz / 4000 Hz label, live stats (ISR cycles out of 1443 and load %, measured fs, late edges, clips), and BACK and FLAT buttons.
- **Spectrum page:** still a placeholder.

Touch areas are `box_t` rectangles checked with `hit()`.

**Rate switching (`select_rate`):**

- It switches the **mixer off before** sending a faster rate, and back **on only after** the fx/8 command plus redraw (milliseconds).
- So the mixer never runs on a 180-cycle budget.

## 5. Clock board control (`rate_ctrl.c` and `spi/clock_spi.c`)

**`rate_ctrl.c`** keeps the state:

- `current_rate` (the `rate_t` enum: 0 = fx up to 3 = fx/8) and `clock_enabled`.
- Lookup tables `rate_cmd[]` (D1/D2/D4/D8), `rate_text[]` and the LEDs (PF0–PF3 rate, PF4 enabled).
- LEDs change with one `BSRR` write, so they update atomically.
- `rate_send_invalid()` sends `0xD3` and changes **no** state, because the board must ignore it.
- `clock_last_cmd` holds the last byte sent, for the screen and the Keil Watch window.

**`clock_spi_send(cmd)`** sends one command as one transaction, about **190 µs**, blocking:

1. SPI1 master, mode 0, MSB first, **281 kHz** (72 MHz / 256), deliberately slow for breadboard wiring. Sends on PA5 (SCLK) and PA7 (MOSI).
2. Waits for `TXE`, writes `SPI1->DR`, waits for `RXNE`, then waits for `BSY` to clear.
3. Pulses **CMD_LATCH** (PC1) high, which moves the byte to the shift register's outputs.
4. Holds **APPLY** (PF10) high for 50 µs. The board syncs it to its own clock and, for a valid byte only, switches cleanly.
5. Then 50 µs of bus silence.

SPI1 is shared with the touch chip, at a different speed. So the function saves `SPI1->CR1`, swaps in its own settings with SPE off, and restores them afterwards. All SPI runs in the main loop, so the two can never interleave.

## 6. LCD and refresh rate

**How the screen connects:**

- The screen is an ILI9341, 240×320, 16-bit RGB565 colour.
- It is on the **FSMC** 16-bit parallel bus: commands go to `0x60000000` and data to `0x60020000`. Address line A16 selects command or data.

**"Refresh rate" means two different things:**

- **Panel refresh: 70 Hz.** The ILI9341 redraws the glass by itself from its own memory (GRAM). The library sets `FRMCTR1 = 0x00, 0x1B`, which is 70 Hz. The MCU isn't involved.
- **Our updates: event-driven.** The MCU writes pixels only when something changes: a touch, a rate change, or the 2 Hz stats lines on the mixer page.

**Why there's no frame buffer:** one would need 240 × 320 × 2 = 150 KB, and the chip has only 48 KB of RAM. Redrawing small regions is also much cheaper than redrawing the whole screen.

**Drawing is in the main loop only.** A full-screen fill takes tens of milliseconds, which would be fatal inside the ISR.

**`text2x()`** draws the 6×8 library font at double size (12×16) by filling 2×2 blocks, so buttons are readable and easy to hit.

## 7. Files at a glance

| File | Owns | Key items |
|---|---|---|
| `SystemInit.c`, `clock.h` | 72 MHz clock tree | `SystemInit`, `SystemCoreClockUpdate`, `SYSCLK_HZ` |
| `debug.c/.h` | LEDs, scope pin, cycle counter | `dbg_cycles()`, `dbg_scope_high/low()`, PF5/PF7/PF8 |
| `adc.c/.h` | ADC1 ch3 on PA3 | `adc_start/is_done/result`, sample time 7.5 cycles, self-calibration |
| `dac.c/.h` | DAC ch1 on PA4 | `dac_write()`, buffered output, no trigger (updates straight away) |
| `extclk.c/.h` | PF11 → EXTI11 | `AFIO->EXTICR[2]`, rising edge only, pull-down, `extclk_edges` |
| `audio.c/.h` | The ISR | `audio_process`, `audio_overruns`, `audio_late_edges`, `audio_max_cycles` |
| `Sampling/sampling.c/.h` | Per-sample conditioning | `sampling_process`, `dc_sum`, `sampling_clips` |
| `ui/mixer.c/.h` | Filters and gains | `mixer_process`, `bank[2][4]`, `live_bank`, `gain[3]`, `mixer_on`, `reset_request` |
| `ui/rate_ctrl.c/.h` | Rate and enable state, LEDs | `rate_set/enable/send_invalid`, `RATE_COUNT`, `clock_last_cmd` |
| `spi/clock_spi.c/.h` | SPI transaction to the board | `clock_spi_send` (CMD_LATCH, APPLY) |
| `ui/touch.c/.h` | Touch controller | `touch_poll`, calibration screen (test 5) |
| `ui/ui.c/.h` | Pages, buttons, stats | `ui_poll`, `ui_touch`, `select_rate`, `stats_update` |
| `tests.c`, `main.c` | Bench tests 2–9, test selection | `ACTIVE_TEST` |

## 8. How main and the ISR share data safely

- **One writer, aligned 32-bit values.** Gains, `mixer_on` and `audio_process` are written only by main and read only by the ISR. A single 32-bit write can't be interrupted halfway, so the ISR always sees either the old value or the new one.
- **Double-buffered coefficients.** Main fills the bank the ISR isn't using, then flips `live_bank` in one write.
- **Handshake for clearing filter memory.** Main sets `reset_request`, and the ISR clears the filter states itself. Main clearing them directly could collide with the ISR mid-update.
- **`volatile` on every shared variable,** so the compiler re-reads them instead of caching them in a register.
- **Pin changes go through `BSRR`.** A read-modify-write of `ODR` could be undone by an interrupt landing in the middle of it.

## 9. Numbers to know

| Item | Value |
|---|---|
| fx | 352 + average of the zID endings (8, 67, 72, 42) = **399.25 kHz** |
| Cycles per sample (fx, fx/2, fx/4, fx/8) | 180 / 361 / 721 / **1443** |
| ADC conversion | 120 CPU cycles (1.67 µs) |
| Audio latency | 2 samples (5 µs at fx, 40 µs at fx/8) |
| Mixer cost | 43 multiply-accumulates per sample, at fx/8 only |
| Clock SPI | 281 kHz, about 190 µs per command |
| Touch SPI | 1.125 MHz, 10 ms poll |
| LCD | 70 Hz panel refresh; event-driven redraws; stats every 0.5 s |
| Board input gain | ×11; keep the input under about 260 mVpp |
| DAC swing | about 0.2 to 3.1 V |

## 10. Likely demonstrator questions

- **"How do you know no samples are dropped?"** Three checks. `OVERRUNS` stays at 0: the ADC always finished in time. `LATE EDGES` stays at 0: no edge arrived while the ISR was busy. A burst of exactly N pulses raises `TOTAL EDGES` by exactly N. On top of that, PF8's pulse width shows the ISR's margin on the scope.
- **"Where is the sampling instant, and why is it jitter-free?"** At `adc_start()`, a fixed number of cycles after the edge, before any variable-length processing.
- **"Why not sample in the main loop?"** Main-loop timing changes with drawing and touch. The spec also requires sampling in an ISR driven by the external timer.
- **"What happens with an invalid command?"** It is shifted in and latched, but the board's decoder rejects it, so APPLY does nothing. The MCU's state isn't changed either. The rate LEDs and scope frequency stay the same; only `LAST CMD` changes.
- **"Why is the mixer only on at fx/8?"** At fx there are 180 cycles per sample, and the mixer alone needs more than that.
- **"Why LR4 and integer maths?"** Equal gains sum flat (within 0.18 dB). The M3 has no floating-point unit. Error feedback stops the low-pass sticking at a DC level.
- **"How is the gain change made safe?"** It is one aligned 32-bit write, and the ISR reads it fresh every sample.
- **"How fast does the screen update?"** The panel refreshes itself at 70 Hz from its own memory. We only write changed regions, all in the main loop.

## 11. Not yet measured

Don't claim these until they are measured:

- The real ISR cycle count with the mixer running (read it off the mixer page).
- The analog anti-aliasing filter, which still needs building and measuring.
