# ELEC4123 Embedded System Design: Project Notes

Term 3 2026. Last updated 3 November 2026.

Everything we've worked out so far lives here. It comes from the task spec (`Embeded Elective Task 2026.pdf`), the APE Board Guide, RM0008 Rev 21, the STM32F103 datasheet Rev 13, PM0056, APE schematic Rev 1.4, and our `ELEC4123_Connected_Clock` simulation files. Anything marked **estimate** is our best guess until we measure it.

## 1. Where we are

| Item | Status |
|---|---|
| Firmware stages 1 to 3 | Written, and running on the board in Keil 5. We still need to record real readings |
| Firmware stage 4 | Written, not yet run on the board |
| Keil project | Moved to Keil 5 (`empty_skeleton.uvprojx`), set to `-O3` |
| Clock hardware | LTspice functional model only. No SPI receiver, invalid-command logic, saved settings, glitch-free switching or power-on reset yet |
| Anti-aliasing and reconstruction | Not started, no owner yet |
| Extension choice | Not decided. As a team of four, we owe three parts |

**What we need to do next**

1. Count LED0 for a minute to settle whether the crystal is 12 or 16 MHz (section 10).
2. Record the Stage 2 readings, because they didn't seem to change much with volume when we tried it.
3. Record the Stage 3 voltages at J1301.
4. Pick our three extension parts. We're leaning towards A.1, A.2 and B.1 (section 8).
5. Decide who owns anti-aliasing and reconstruction, because the spec wants a measurable result (section 7).
6. Tune the real clock to 399.25 kHz or slightly below, not above (section 3).

## 2. What the task requires

This is the spec boiled down, so we can check our work without digging through the PDF.

**Primary objective:** sample audio inside an interrupt service routine triggered by our own external hardware timer, with the rate set over SPI from the microcontroller.

**Secondary objective:** an LCD touchscreen interface with application buttons and page switching to the extension tasks.

### Task load

| Team size | Required |
|---|---|
| 2 | Core + 1 extension part |
| 3 | Core + 2 extension parts |
| **4 (us)** | **Core + 3 extension parts** |

- A.1 must work before A.2, and B.1 before B.2.
- Extensions A and B are independent of each other.
- Doing more than the required load earns no extra marks.
- The bonus task is optional and should be the lowest priority.

### Sampling clock

Conditions:

- Generated from hardware.
- No clock or timing signal from the APE board may generate or discipline it.
- No lab signal generator in the delivered design. Using one to test is fine.
- The MCU's timers may measure the rate, but sample timing must not come from them.
- Powered from the 3.3 V APE output.

Requirements:

- Four selectable rates: fx, fx/2, fx/4, fx/8.
- Accuracy ±50% on each rate.
- Exactly one rate is presented to the MCU at any time.
- The selected rate holds indefinitely until a valid command changes it.
- Enable and disable on command, keeping the selected rate across a disable and enable.
- Logic-level output suitable for direct connection to the APE board.

### SPI commands

| Command | Action |
|---|---|
| `0xD1` | Output fx |
| `0xD2` | Output fx/2 |
| `0xD4` | Output fx/4 |
| `0xD8` | Output fx/8 |
| `0xE0` | Disable timer |
| `0xE1` | Enable timer |
| `0xFX` | Output the closest available frequency to X kHz (bonus only) |

- The link is one-directional. The MCU may drive data, clock and any strobe or latch line.
- Nothing needs to come back from the timer to the APE board.
- An invalid command must not change, reset or glitch the rate or enable state, during or after reception and latching.
- A valid command changes the rate cleanly.

### Audio path

Conditions:

- Audio enters at the APE audio jack and leaves through the APE speaker.
- The core runs at 72 MHz from its own clocks, unrelated to the sampling clock.
- Sample timing must not come from any internal MCU timer.

Requirements:

- Sampling happens in an ISR triggered by the external timer. Polling or sampling from the main loop fails this.
- Output sample rate equals input sample rate.
- The main page passes audio with no processing.
- Continuous operation at all four rates with no dropped and no repeated samples. We must convince the marker of this.
- The output reproduces the input.
- Input aliasing and output reconstruction artefacts must be addressed, and we must be able to measure the effect.
- Recovers after a rate change without a reset.

### User interface

- All runtime control goes through the touchscreen.
- Page switching to each extension part attempted.
- Rate selectable, with the current rate displayed.
- Timer enable and disable from the interface.
- Stays responsive while audio runs. Audio must not drop out during redraws or touches.

### Extension A: 3-band audio mixer

Applies to both parts:

- The core audio path keeps meeting its requirements.
- All gain and cutoff control is through the touchscreen.
- Demonstrated through the speaker and measured at the DAC output.
- Maximum input frequency 5 kHz.
- Filter coefficients may come from MATLAB or similar, but we must understand the design method.
- We must explain the method, its computational cost, and how close it runs to the processor's limit.

| Part | Requirements |
|---|---|
| A.1 | One band, real time at fx/8. Gain 0 to 2. Measured response matches what the interface requests. No dropped samples, with a method to demonstrate it |
| A.2 | Three bands, able to run at fx/8. Cutoffs 500 Hz and 4 kHz. Each gain independently 0 to 2. Tone colour preserved. Measured response matches. No dropped samples |
| Soft | Configurable cutoffs within 50% either side |

Note: the on-board speaker plays low frequencies poorly.

### Extension B: frequency analysis

Applies to both parts:

- The core audio path keeps meeting its requirements.
- Runs at all four sample rates.
- Displayed range is 0 to 20 kHz.

| Part | Requirements |
|---|---|
| B.1 | 128-point FFT, 128 bins. Refresh at least 30 times a second, verified with a GPIO toggle on a scope. No dropped samples |
| B.2 | Fundamental frequency shown as a number. ±1% accuracy from 100 Hz to 2 kHz for sine, square and triangle inputs. Updates at least 5 times a second |
| Soft | 8-bin power spectrum display. Time-domain display that holds position for a steady input |

### Bonus: frequency request

- Implement `0xFX`. The hardware selects the available rate nearest the requested kHz value.
- That means nearest, not the highest rate below the target.
- Selection is in hardware. The MCU must not compute the rate or send a rate index.
- The link stays one-directional, and invalid-command behaviour still applies.

### Rules and restrictions

**Parts:** APE board, 74HC595, 74HC153, 74HC93, 74HC74, 74HC02, 74HC00, 74HC14, plus components allowed in the Electronics topic. No other microcontroller, programmable logic device or microprocessor. Test equipment cannot be part of the delivered system.

**Firmware:**

- Keil uVision 4 or 5.
- No HAL functions or external libraries beyond those provided.
- Everyone must be able to explain every line of submitted code. Failing to explain a line can be heavily penalised.

**Hardware safety:**

- Never short VCC to GND on the APE board. The fuse will blow.
- Never short the fuse slot. Ask a demonstrator for a new fuse.
- Do not disassemble the hardware.
- Do not drive a low-impedance load without a driver or amplifier.
- Do not use a separate power supply. All power comes from the USB-C supply.

### Demonstration and deliverables

At the live demo we must:

1. Pass audio at all four rates, selected from the touchscreen.
2. Show the measured timer frequency at each setting.
3. Send an invalid command and show that rate and enable state do not change or glitch.
4. Demonstrate each attempted extension part against its requirements.

We must also have prepared:

1. A neat schematic of the timer hardware and control logic.
2. A description of the decode, including which byte patterns are treated as invalid.
3. Measured results for each requirement we claim.
4. A journal with working, measurements, design decisions and dated progress.
5. A test plan so the demo order is ready.

## 3. Our frequencies

zID endings: 72, 67, 08, 42. Average = 189 / 4 = 47.25, so fx = 352 + 47.25 = **399.25 kHz**.

| Rate | Frequency | Period | CPU cycles at 72 MHz | ±50% window |
|---|---|---|---|---|
| fx | 399.25 kHz | 2.505 µs | 180 | 199.6 to 598.9 kHz |
| fx/2 | 199.625 kHz | 5.009 µs | 361 | 99.8 to 299.4 kHz |
| fx/4 | 99.8125 kHz | 10.019 µs | 721 | 49.9 to 149.7 kHz |
| fx/8 | 49.90625 kHz | 20.038 µs | 1,443 | 25.0 to 74.9 kHz |

**The ±50% allowance doesn't really help us.** It looks generous, but a clock at the top of the window gives only 120 cycles per sample, and our ADC plan stops fitting at roughly 450 kHz (**estimate**). So we're aiming the hardware at 399.25 kHz or slightly lower.

**We have to measure the real rate.** Our clock is an RC oscillator, so it won't sit exactly on 399.25 kHz. Mixer coefficients, FFT frequency labels and pitch estimates need the measured rate, not the nominal one. The spec lets us measure it with the chip's own timing, and the Stage 4 test already does.

## 4. Team and interfaces

Each of us owns one area, and these are the places where our work meets.

| Owner | Scope |
|---|---|
| Core firmware and integration | Clock tree, ADC, DAC, EXTI, ISR, SPI commands, buffering, measurements, integration |
| Clock hardware | Oscillator, dividers, SPI receiver, decode, glitch-free switching, power-on state |
| DSP | Mixer and EQ design, coefficients, computational cost |
| LCD and FFT | Touch interface, pages, spectrum display |

| Boundary | Direction | Agreement |
|---|---|---|
| Firmware to DSP | ISR calls one short fixed-point function per sample | No float, no blocking, fixed cycle budget |
| UI to DSP gains | Main writes, ISR reads | Single 32-bit aligned values, or an atomic swap of parameter sets |
| Firmware to FFT | ISR fills a 128-sample buffer, main reads it when full | One flag passes ownership. The ISR never waits |
| UI to clock board | Main calls a rate function | All SPI happens in main only |
| Clock board to firmware | Rising edge on PF11 | 3.3 V logic, clean single edges |

**Why all our SPI stays in main:** the touch controller, EEPROM and clock board all share SPI1. Our clock board's shift register will see touch traffic on SCK and MOSI, so it must only latch on its own chip select.

```text
 J1302 jack -> MAX4466 (gain 11) -> PA3 / ADC1 channel 3
                                        | conversion runs in the background
 Clock board -- PF11 / EXTI11 --> ISR (highest priority)
      ^                             read ADC -> process -> write DAC -> PA4 -> MAX9700 -> speaker
      | SPI1, CS on PC1             +-> FFT buffer
      |
 Main loop (lowest priority): touch, LCD, FFT, rate changes
```

## 5. Board reference

The board facts we kept looking up in the schematic.

### Pins

| Function | Pin | Location | Notes |
|---|---|---|---|
| Audio in | PA3 | MAX4466 output | Schematic net `ADC2` is ADC channel 3. Net names are offset by one |
| Audio out | PA4 | J1301 test point, J1303 is GND | DAC channel 1 |
| External clock | **PF11** | J1101 pin 6 | Next to breadboard. EXTI line 11 |
| Spare trigger | PF9 | J1101 pin 4 | EXTI line 9 can trigger the DAC |
| Scope debug | PF8 | J1101 pin 3 | Our timing marker pin |
| LED0 | PF0 | Green, active high | Heartbeat |
| LED7 | PF7 | Red, active high | Clock fault |
| SPI1 | PA5 SCK, PA6 MISO, PA7 MOSI | J901 to J904, test port J1701 | Shared bus |
| Clock chip select | PC1 | J901 pin 6 | Active low |
| EEPROM CS | PC0 | | Keep high |
| Touch CS | PC5 | | Keep high when not in use |
| Touch IRQ | PA0 and PE5 | J501 must be on the left side | EXTI0 for touch |
| Switches | PC6, PC7 | SW0, SW1 | |
| Push buttons | PE0, PE1 | PB0, PB1 | |

### Headers

| Header | Pinout |
|---|---|
| J901 | 1 GND, 2 3V3, 3 MISO, 4 SCK, 5 MOSI, 6 CS1 (PC1) |
| J1101 | 1 3V3, 2 GND, 3 PF8, 4 PF9, 5 PF10, 6 PF11, 7 3V3, 8 GND |
| J1102 | 1 3V3, 2 GND, 3 PF12, 4 PF13, 5 PF14, 6 PF15, 7 3V3, 8 GND |
| J1701 digital probes | 5 SCK, 7 MISO, 9 MOSI |

### Peripheral ownership

- **LCD library:** owns the FSMC pins on ports D and E, PB0, PG13 and TIM5.
- **Never touch:** `GPIOD`, `GPIOE` control registers after `lcd_init`, or USART2 remap in `AFIO->MAPR`.
- **EXTI15_10 vector:** shared with PD12 (Wi-Fi IRQ) and PD13 (expander IRQ) if anyone enables those lines.

### Analog path

**Input:** L and R merge through 220 Ω resistors, then pass through a 10 nF coupling capacitor into a 1.65 V bias network and the MAX4466.

- Gain is 1 + 100k/10k = 11, so about **300 mVpp at the jack reaches the ADC limits**. We have to keep the source volume low.
- High-pass corners are about 16 and 32 Hz.
- C1308 forms a shelf from 15.9 kHz to 175 kHz, not a true low-pass.
- MAX4466 roll-off adds some attenuation, but we don't have its datasheet.

**Output:** PA4 goes through R1304 and the R1303 volume pot, then 1 µF into the MAX9700 class-D amplifier.

- There's no reconstruction filter on the board, so that one's on us.
- The DAC's buffered output swings about 0.2 V to 3.1 V, so it isn't rail to rail.
- Full-scale settling is 3 to 4 µs, longer than one fx period. Small steps are fine, with updates up to 1 MS/s.
- Speaker resonance is about 1.7 kHz, so we test above 1.5 kHz.

**Power:** VREF+ and VDDA tie directly to digital 3V3, with no separate analog filter, so we expect some digital noise on ADC readings.

## 6. Firmware design

### Clock tree

| Clock | Rate | Used by |
|---|---|---|
| SYSCLK, HCLK | 72 MHz | CPU, FSMC |
| PCLK2 (APB2) | 72 MHz | GPIO, AFIO, EXTI, ADC1, SPI1 |
| PCLK1 (APB1) | 36 MHz | DAC |
| TIM2 to TIM7 | 72 MHz | TIM5 owned by the LCD |
| ADCCLK | 12 MHz | ADC (PCLK2 / 6, limit 14 MHz) |

### ADC timing

One ADC clock is 6 CPU cycles.

| Sample time | Conversion | CPU cycles | Fits fx (180 cycles)? |
|---|---|---|---|
| 1.5 | 14 clocks | 84 | Yes, least settling time |
| **7.5** | **20 clocks** | **120** | **Yes, planned setting** |
| 13.5 | 26 clocks | 156 | Too tight with software start |
| 28.5 | 41 clocks | 246 | No |

### The pipelined ISR

Starting the ADC and waiting for it inside the ISR would use most of the fx budget. So instead, each interrupt collects the conversion started on the previous edge.

Here's the order we're planning, which we'll confirm in Stage 5:

1. Clear the EXTI pending flag.
2. Write the already-computed output sample to the DAC.
3. Read the ADC result from the previous edge, checking EOC.
4. Start the next conversion.
5. Run passthrough or the mixer, and store the next output.
6. Add to the FFT capture if it's active.

Steps 2 and 4 run at a fixed time after the edge, so DSP length doesn't add jitter. It costs us a little extra latency: one or two sample periods.

### Why sampling starts in software by default

The spec says sampling happens in the ISR, so software start is the literal fit. A rising edge can also start ADC1 directly through EXTI line 11 (RM0008 Table 67), which removes ISR jitter. That's why we put the clock on PF11, but we'll only use that if a demonstrator confirms it counts as sampling in the ISR.

Jitter of about 70 ns rms limits SNR to about 41 dB for a full-scale 20 kHz tone and 62 dB at 1.7 kHz.

### Interrupt priorities

| Priority | Item |
|---|---|
| Highest | External clock EXTI |
| Lower | Touch IRQ, flag only |
| Lowest | Main loop: LCD, FFT, SPI |

We need to keep any `__disable_irq` section to a few instructions, because a long critical section delays a sample.

### Rate changes

- We change rates from main only, and let the hardware switch the clock safely.
- The ISR throws away the first result after a restart, because it may be stale.
- On power-up, we always send a rate and then enable, so we don't rely on the hardware's start state.

## 7. Performance and risks

### ISR cycle estimate at fx

| Part | Cycles (**estimate**) |
|---|---|
| Interrupt entry with Flash wait states | 14 to 20 |
| Register work in the ISR | 25 to 40 |
| Interrupt exit | 14 to 20 |
| **Passthrough total** | **55 to 80 of 180 (about 30 to 45% CPU)** |

We'll measure this properly in Stage 5, because the spec needs us to show how close we run to the limit.

### Risk register

| Risk | Impact | Plan |
|---|---|---|
| Crystal is 16 MHz, not 12 | CPU would run at 96 MHz, out of spec | Stage 1 test, one-line fix in `clock.h` |
| Clock above about 450 kHz | ADC conversion misses the next edge | Keep real fx at or below 399.25 kHz |
| No filtering for aliasing | Fails a stated requirement, worst at fx/8 | Add and measure an input filter (below) |
| Clock edges ringing on long wires | Double interrupts, false repeated samples | Short wires with a ground return, check on scope |
| Mixer page at rates above fx/8 | Might not fit the budget | Bypass above fx/8, show this on screen, confirm with demonstrator |
| FFT at fx | Only about 6 bins in 0 to 20 kHz without decimation | Decimate before the FFT (below) |
| Touch and clock board on same SPI | Corrupted commands | All SPI in main, one owner |
| Noisy ADC from shared 3V3 | Audible hiss | Measure in Stage 2 |

### Mixer budget

The spec only requires the mixer at fx/8, which gives about 1,440 cycles per sample. After ISR overhead and a margin for the UI, our working budget is about 900 cycles (**estimate**).

A.2's "tone colour preserved" means the bands must sum back cleanly at equal gains. There are two options we're considering:

- **Linkwitz-Riley IIR crossovers:** cheap, and they sum flat.
- **One linear-phase FIR:** built by adding the three band responses, weighted by the gains, and recomputed in main when a gain changes.

### FFT plan

| Rate | Bin width at 128 points | Bins in 0 to 20 kHz |
|---|---|---|
| fx | 3,119 Hz | 6.4 |
| fx/2 | 1,560 Hz | 12.8 |
| fx/4 | 780 Hz | 25.6 |
| fx/8 | 390 Hz | 51.3 |

At fx, a plain 128-point FFT would give us only 6 bins across the whole display. So our plan is for the ISR to average 8, 4, 2 or 1 samples down to about 49.9 kHz, which makes the FFT look the same at every rate. Each frame then spans 2.56 ms.

| Work per 33 ms frame at fx | Cycles (**estimate**) |
|---|---|
| Available to main | About 1.3 million |
| 128-point fixed-point FFT | 15,000 to 25,000 |
| Redrawing a 128 × 100 pixel area | About 350,000 |

### Why B.2 is not recommended

100 Hz has a 10 ms period, but a 128-sample frame at 49.9 kHz lasts only 2.56 ms, so one frame doesn't even hold a full cycle. Getting ±1% accuracy would need a longer capture and a separate estimator, which adds risk for no extra marks over A.1, A.2 and B.1.

### Aliasing and reconstruction

This is a stated requirement, so we need a measured before-and-after result.

**Input aliasing:** this is worst at fx/8, where Nyquist is 24.95 kHz. The output rate has to equal the input rate and we can't modify the board, so the practical fix we've landed on is an analog low-pass filter between the audio source and the jack.

- We're targeting a cutoff around 20 kHz, powered only from APE 3.3 V if it's active.
- **Test:** inject 30 kHz at fx/8 and compare the 19.9 kHz alias with and without the filter.

**Output reconstruction:** the DAC holds each sample, creating images near multiples of the sample rate.

| Effect at fx/8 (calculated) | Level |
|---|---|
| Droop at 5 kHz | −0.14 dB |
| Droop at 20 kHz | −2.42 dB |
| Image of a 5 kHz tone at 44.9 kHz | About −19 dB below the tone |

The speaker and class-D stage remove some of this acoustically. For an electrical result, we can add an RC filter on a tap from J1301 and compare spectra before and after. We'll check with a demonstrator that this addresses the requirement.

## 8. Recommended extension choice

We're leaning towards **A.1, A.2 and B.1**, for these reasons:

- This matches our existing DSP and FFT roles.
- A.2 only has to run at fx/8.
- B.1 is well within the estimated budget with decimation.
- B.2 is the riskiest part and earns nothing extra over this set.

## 9. Firmware stage plan

| Stage | Work | Status |
|---|---|---|
| 1 | Clock tree, debug tools | Running, LED count still to do |
| 2 | ADC on PA3: calibration, sample time, noise check | Running, volume response still unclear |
| 3 | DAC on PA4: fixed value, then J1301 check | Running, J1301 reading still to do |
| 4 | EXTI on PF11, with a signal generator as our stand-in clock | Written, not yet run on the board |
| 5 | Passthrough ISR, cycle measurement | Not started |
| 6 | SPI driver, rate and enable commands, invalid-command test button | Not started |
| 7 | DSP hook, gain updates, FFT capture and decimation, measured rate | Not started |
| 8 | Full measurements, no-drop evidence, filter results | Not started |

## 10. Stage 1 guide

### Files

| File | Purpose |
|---|---|
| `Sources/clock.h` | Clock constants, including `HSE_HZ` |
| `Sources/SystemInit.c` | Clock startup, runs before `main()` |
| `Sources/debug.c`, `debug.h` | LED, scope pin, cycle counter |
| `Sources/main.c` | Starts the board and picks which test runs |

### Boot sequence

1. BOOT0 is low (J502), so Flash maps to address 0.
2. The CPU loads the stack pointer and `Reset_Handler` from the vector table.
3. `Reset_Handler` calls `SystemInit()`.
4. `__main` copies `.data`, zeroes `.bss` and calls `main()`.

Anything `SystemInit()` writes to a global gets overwritten in step 4, which is why `main()` reads the clock state back from the registers. `main()` also must never return, because exit ends in a semihosting breakpoint that faults without a debugger.

### Clock sequence

| Step | Register | Why |
|---|---|---|
| 2 wait states, prefetch on | `FLASH_ACR` | Required above 48 MHz, and set before speeding up |
| Move to HSI, stop PLL | `RCC_CR`, `RCC_CFGR` | PLL settings only change while it's off |
| Start crystal, wait | `HSEON`, `HSERDY` | If it fails, stay on HSI and report a fault |
| Set dividers | `RCC_CFGR` | AHB /1, APB2 /1, APB1 /2, ADC /6 |
| Select PLL source and multiplier | `PLLSRC`, `PLLMUL` | 12 MHz × 6, or 16 MHz ÷ 2 × 9 |
| Start PLL, wait | `PLLON`, `PLLRDY` | Lock takes up to 200 µs |
| Switch to PLL, confirm | `SW`, `SWS` | SW requests it, SWS confirms it |

**Easy mistake:** `FLASH_ACR_LATENCY_2` in this header is `0x4`, which means bit 2, not 2 wait states. Our code uses `2U << FLASH_ACR_LATENCY_Pos`.

### Build and flash

1. Open `empty_skeleton.uvprojx` in Keil 5. Check that our source files are listed under Sources.
2. In Options for Target, under C/C++, confirm **Level 3 (-O3)**. Leave Cross-Module Optimization off.
3. Press F7 and expect 0 errors.
4. Press F8, then RESET on the board.
5. From a Mac, we build on a lab PC in room 202 or 217, or in a VM. In STM32CubeProgrammer, we connect, open Erasing & Programming, choose the `.axf`, tick Verify and Run after programming, then start.

If the build reports `Cannot find argument 'Reset_Handler'`, the startup file wasn't assembled. Right-click it, open Options, and tick Include in Target Build.

### Test

Our first test put 1 kHz on PF8, but later stages replaced that loop. LED0 still works as the check in the Stage 2 and Stage 4 tests, so we count its flashes for one minute. The PF8 column below applies to commit `4fccd1a`.

| PF8 | LED0 per minute | Meaning | Action |
|---|---|---|---|
| **1.000 kHz** | **60** | 72 MHz, crystal is 12 MHz | Pass |
| 1.333 kHz | 80 | Crystal is 16 MHz, CPU at 96 MHz | Set `HSE_HZ` to `16000000U`, rebuild straight away |
| 111 Hz | 6.7 | On HSI, red LED7 and `CLOCK FAULT` show | HSE or PLL failed |

Expected values on the original Stage 1 screen:

| Field | 12 MHz crystal | 16 MHz after fix |
|---|---|---|
| SYSCLK (Hz) | 72000000 | 72000000 |
| RCC_CFGR (hex) | 11840A | 1F840A |
| FLASH_ACR (hex) | 32 | 32 |

SYSCLK trusts `HSE_HZ`, so only the LED or PF8 result proves the frequency.

### Troubleshooting

| Symptom | Likely cause |
|---|---|
| Nothing runs after reset | J502 not at GND, or Keil Flash algorithm not set |
| Backlight on, screen blank | `lcd_init` didn't finish. Power-cycle first |
| LCD garbage at 72 MHz | Flash latency wrong. `FLASH_ACR` should read 32 |
| Keil error `#268` | armcc is in C90 mode here. Put declarations at the top of blocks |
| No signal on PF8 | Probe on wrong pin. Pin 1 is 3V3, pin 3 is PF8 |

## 11. Proving each requirement

We have to convince the marker, so this is the evidence we plan to show for each requirement.

| Requirement | Evidence | Tool |
|---|---|---|
| Four clock rates | Frequency at each setting | Scope |
| Invalid commands ignored | Persistent scope trace while sending bad bytes from a UI test button | Scope, trigger on PC1 |
| Rate held across disable | Same frequency before and after | Scope |
| No dropped samples | EOC-not-set and pending-at-exit counters stay at zero | Firmware counters on LCD |
| No repeated or skipped samples | Ramp test: ISR outputs a counter to the DAC, so any glitch breaks the staircase | Scope at J1301 |
| Interrupt count matches clock | ISR count against an independent timer count | Timer, allowed by the spec for measurement |
| CPU headroom | ISR marker pin high time, DWT cycle counts, min and max | Scope and counters |
| Sampling jitter | Marker edge against clock edge, persistence mode | Scope |
| Audio reproduces input | Sine input against J1301 output | Scope, two channels |
| Rate-change recovery | Change rates under audio, counters stay clean | Scope and counters |
| Audio during UI use | Repeat counter test during redraw and touch | Firmware counters |
| Mixer response | Measured response against requested | Sweep at J1301 |
| FFT refresh of at least 30 Hz | GPIO toggle per frame | Scope |
| Aliasing and reconstruction | Before-and-after spectra | Scope FFT or spectrum tool |

We can't rely on software counters alone, because a lost edge never reaches firmware. That's why we want the ramp test and an independent count. With a signal generator, a burst of an exact number of pulses gives us that count, and the Stage 4 test can already read it.

## 12. Open questions

**What we need to ask a demonstrator**

1. Does starting the ADC from EXTI line 11 count as sampling in the ISR, if the ISR collects the result?
2. What does "128-point FFT, 128 bins" mean? A real input gives only 64 unique bins up to Nyquist.
3. Is decimating to about 49.9 kHz before the FFT acceptable at higher rates?
4. Can the mixer bypass at rates above fx/8?
5. Is an external filter before the jack, plus a measured filter from J1301, acceptable for the aliasing and reconstruction requirement?
6. Is a crystal allowed in the clock, or RC only? This affects jitter.

**What we need to decide as a team**

1. Who owns the input and output filters?
2. Are we confirmed on the three extension parts?
3. Which SPI settings are we using? The guide suggests mode 0, MSB first, at PCLK2 / 256.
4. Who has Keil access? The spec promises a Mac and Linux workaround, and the guide's route is a lab PC plus STM32CubeProgrammer.

## 13. Mistakes we found in the documents

Errors we found in the documents.

| Where | Issue | What we do |
|---|---|---|
| Schematic X501 | Labelled 12 MHz, part number reads 16 MHz | Stage 1 test decides |
| Board Guide Ch 9 | `NVIC->ISER[k/32] = (k%32)` is wrong | Should set `1 << (k%32)`. Use `NVIC_EnableIRQ` |
| `lcd_hw.h` | FSMC pointers aren't `volatile` | Writes are safe. `ili9341_ReadID()` is unreliable at `-O3`, so don't use it |
| `lcd_hw.c` | `LCD_mdelay` isn't in microseconds | One tick is about 0.67 ms at 72 MHz |
| Quick reference sheet | I2C read and write addresses are swapped | Use Board Guide Ch 7 |
| `empty_skeleton.plg` | The tutor's saved build failed, startup not assembled | See Stage 1 build notes |
| `LCD/stm32f103xe.h` | Needs `core_cm3.h`, which isn't in `LCD/` | `.\Headers` is in the Keil include paths |
