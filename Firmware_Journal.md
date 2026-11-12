# Firmware Journal

My part of the project is the core audio firmware and getting everyone's pieces working together. This journal goes stage by stage: what each part does, why I did it that way, and how we can prove it at the demo. The dates come from our git commits.

If you're a teammate skimming this, the last section ("What each of you needs from the firmware") is the short version for your part.

---

## The numbers everything is built around

Our base rate comes from the spec: fx = 352 kHz plus the average of the last two digits of our four zIDs. Our endings are 72, 67, 08 and 42, so the average is 189 ÷ 4 = 47.25, and fx = 399.25 kHz.

The chip runs at 72 MHz, so at each rate this is how many CPU cycles I get per sample:

| Rate | Frequency | Time per sample | CPU cycles per sample |
|---|---|---|---|
| fx | 399.25 kHz | 2.505 µs | 180 |
| fx/2 | 199.625 kHz | 5.009 µs | 361 |
| fx/4 | 99.8125 kHz | 10.019 µs | 721 |
| fx/8 | 49.90625 kHz | 20.038 µs | 1,443 |

180 cycles is the number that drives almost every decision below. That's not much. Getting into and out of an interrupt alone costs around 30 cycles, and a single ADC conversion takes 120. So at the top rate, nothing in the sampling path can afford to wait or do anything slow.

---

## Tuesday 3 November

### Stage 1: Clock tree and debug tools

*Committed 10:12, `4fccd1a`*

**What it does:** the chip wakes up running at 8 MHz off its internal clock. Stage 1 switches it to 72 MHz using the 12 MHz crystal on the board. It sets Flash memory to wait 2 cycles per read so it can keep up, sets a safe speed for each bus, and then blinks a heartbeat LED once a second so I can check the speed from outside the chip. The heartbeat was LED0 at first. It's been LED5 since 5 November, because the UI's rate LEDs needed PF0 to PF4.

**Why it matters:**

- At 8 MHz I'd only get about 20 cycles per sample at 399.25 kHz, which is hopeless. At 72 MHz I get 180.
- The spec says the core has to run at 72 MHz from its own clock sources.
- The ADC, DAC, SPI, interrupts and every timing measurement all depend on these clocks being right.
- It settles the crystal question. The Board Guide says the crystal is 12 MHz, but the part number on the schematic says 16 MHz. If the code assumed 12 and the crystal was really 16, the CPU would quietly run at 96 MHz, which is outside the chip's rating.

**How the code works:** `clock.h` gives every clock speed a name, so the rest of the code says `SYSCLK_HZ` instead of `72000000`. `SystemInit.c` does the actual switch. It runs straight from the reset handler, before the C runtime has set up global variables, so anything it stored in a global would get wiped. That's why it only sets hardware registers, and `main()` reads the result back afterwards.

The order of the steps matters, because doing them in the wrong order can crash the chip.

**1. Slow Flash down first.** Flash memory can't keep up with a 72 MHz CPU, so the CPU has to wait 2 extra cycles on every read. RM0008 section 3.3.3 says 2 wait states are needed above 48 MHz. If I sped up first, the CPU would read garbage instructions before the wait states were in place. One trap here: the header has a `FLASH_ACR_LATENCY_2` that looks like "2 wait states", but it actually means bit 2, which is a value of 4. So I shift the 2 in myself.

```c
FLASH->ACR = FLASH_ACR_PRFTBE | (2U << FLASH_ACR_LATENCY_Pos);
```

**2. Get to a known starting state.** I turn on the internal 8 MHz clock (HSI), make sure the chip is actually running on it, then turn the PLL off and wait until it's confirmed off. After a normal power-on this is already the state. But a debugger reset can land here with the PLL still running, and the manual says the PLL's settings can only be changed while it's off (RM0008 pages 99 and 102).

```c
RCC->CR |= RCC_CR_HSION;
wait_for(&RCC->CR, RCC_CR_HSIRDY, RCC_CR_HSIRDY);
RCC->CFGR &= ~RCC_CFGR_SW;
wait_for(&RCC->CFGR, RCC_CFGR_SWS, RCC_CFGR_SWS_HSI);
RCC->CR &= ~RCC_CR_PLLON;
wait_for(&RCC->CR, RCC_CR_PLLRDY, 0U);
```

**3. Start the crystal (HSE).** It takes about 2 ms to start (datasheet Table 23). `wait_for()` only checks a fixed number of times before giving up, so a dead crystal can't hang the board forever. If it fails, the chip stays at 8 MHz, the red LED lights and the screen shows `CLOCK FAULT`. I'd rather the board keep running and say what's wrong than freeze with no clue.

```c
RCC->CR |= RCC_CR_HSEON;
if (!wait_for(&RCC->CR, RCC_CR_HSERDY, RCC_CR_HSERDY)) {
	return;
}
```

**4. Set the speed of each bus.** I set all of these while still at 8 MHz, so no bus ever runs faster than its limit, even for a moment.

```c
RCC->CFGR = RCC_CFGR_HPRE_DIV1		// CPU and main bus = 72 MHz
          | RCC_CFGR_PPRE2_DIV1		// APB2 (GPIO, EXTI, ADC, SPI1) = 72 MHz
          | RCC_CFGR_PPRE1_DIV2		// APB1 (DAC) = 36 MHz, its limit
          | RCC_CFGR_ADCPRE_DIV6	// ADC = 12 MHz, limit is 14
          | RCC_CFGR_PLLSRC			// PLL input is the crystal
          | PLL_SETTINGS;			// x6, so 12 MHz becomes 72 MHz
```

The ADC divider is the interesting one. The ADC's limit is 14 MHz. Dividing 72 MHz by 4 would give 18 MHz, which is too fast, so ÷6 = 12 MHz is the fastest legal option.

**5. Start the PLL, then switch to it.** The PLL (phase-locked loop) is what turns 12 MHz into 72 MHz. Inside it is an oscillator that can run at lots of speeds. It divides its own output by 6, compares that with the crystal, and speeds up or slows down until the two match. Once they match it's "locked", running at exactly 6 × 12 MHz. I have to wait for that lock before moving the CPU onto it. `SW` is my request and `SWS` is the hardware confirming the switch actually happened.

```c
RCC->CR |= RCC_CR_PLLON;
if (!wait_for(&RCC->CR, RCC_CR_PLLRDY, RCC_CR_PLLRDY)) {
	return;
}
RCC->CFGR |= RCC_CFGR_SW_PLL;
wait_for(&RCC->CFGR, RCC_CFGR_SWS, RCC_CFGR_SWS_PLL);
```

**Debug tools (`debug.c`):** these are the tools every later stage uses to see what's going on inside the chip.

- **Heartbeat LED (LED5):** shows the clock speed and that the main loop is still alive.
- **Red fault LED (LED7):** lights if the clock didn't reach 72 MHz.
- **Scope pin (PF8):** later stages use it to show exactly when the interrupt runs.
- **CPU cycle counter:** counts every CPU cycle, so I can time things to the cycle.

```c
CoreDebug->DEMCR |= CoreDebug_DEMCR_TRCENA_Msk;
DWT->CYCCNT = 0U;
DWT->CTRL |= DWT_CTRL_CYCCNTENA_Msk;
```

Pins always change through `BSRR`, which sets or clears a pin in a single write. If I used `ODR ^= ...` instead, which reads, changes and writes back, an interrupt landing halfway through could have its own pin change undone.

**How I know it works:** `SystemCoreClockUpdate()` reads the registers and works out the CPU speed, and the screen shows it. That alone doesn't prove anything, though, because it trusts the crystal value I typed into `clock.h`. The real proof is the LED. The code waits a fixed number of CPU cycles between flashes, so if the chip ran at the wrong speed, the blink would come out wrong by the same ratio. A stopwatch can see that even when the chip can't.

| Flashes per minute | CPU speed | Meaning |
|---|---|---|
| 60 | 72 MHz | Correct, 12 MHz crystal |
| 80 | 96 MHz | Crystal is actually 16 MHz |
| About 7 | 8 MHz | The crystal or PLL never started |

**At the demo:** if a marker asks how we know the core really runs at 72 MHz, the answer is the LED or scope measurement, not the number on the screen. If they ask why the Flash line comes first, it's because the CPU would crash reading Flash too fast during the switch.

---

### Stage 2: Reading audio with the ADC

*Committed 10:44, `0451a52`*

**What it does:** sets up the ADC to read the audio jack on PA3, and shows the readings on the LCD twice a second: the average, lowest, highest and swing. This test reads from the main loop, which is fine for checking the ADC works. The real sampling has to happen inside the external clock's interrupt, which comes in Stage 5.

**Why it matters:** this is how audio gets into the chip. A few facts from the schematic (sheet 13) explain the numbers:

- **Silence reads about 2048 (1.65 V),** because two 1 MΩ resistors bias the input amplifier at half of 3.3 V. Audio swings above and below that.
- **The board amplifies the jack 11 times:** 1 + 100k/10k. So anything bigger than about 300 mVpp at the jack hits the ADC's limits and clips.
- **The schematic calls this net `ADC2`,** which is confusing, but it goes to PA3, which is ADC channel 3.

**How the code works:** `adc_init()` turns on the clocks, makes PA3 an analog pin (which disconnects its digital circuitry), and sets the ADC to do one conversion each time software starts it. It picks channel 3, powers the ADC up, waits for it to wake (1 µs), and runs its self-calibration. RM0008 section 11.4 recommends calibrating after every power-up because it improves accuracy.

```c
ADC1->CR2 = ADC_CR2_EXTTRIG | (7U << ADC_CR2_EXTSEL_Pos);	// software start
ADC1->SQR3 = ADC_AUDIO_CHANNEL << ADC_SQR3_SQ1_Pos;			// channel 3
ADC1->CR2 |= ADC_CR2_ADON;									// power up
ADC1->CR2 |= ADC_CR2_RSTCAL;								// then CAL: self calibration
```

**Why 7.5-cycle sampling:** the ADC spends some clocks sampling the input, then 12.5 clocks converting it. At 12 MHz, each ADC clock is 6 CPU cycles:

| Sample time | Total | CPU cycles | Fits in 180? |
|---|---|---|---|
| 1.5 clocks | 14 clocks | 84 | Yes, but the least time to settle |
| 7.5 clocks | 20 clocks | 120 | Yes, with room to spare. **This is what I chose** |
| 13.5 clocks | 26 clocks | 156 | Too tight once the interrupt overhead is added |
| 28.5 clocks | 41 clocks | 246 | No |

7.5 is the longest sample time that still fits comfortably at the top rate. A longer sample time gives the input more time to settle, which means more accurate readings. To compare settings, change `ADC_SAMPLE_TIME` in `adc.h`.

**What the screen numbers mean:** `MIN` and `MAX` are the lowest and highest readings in each half second, and `SWING` is `MAX − MIN`, which shows how loud the signal is. One code is about 0.8 mV. As an example, a 100 mVpp tone becomes 1100 mV after the ×11 amplifier, running from 1100 to 2200 mV. That should read `MIN` ≈ 1365, `MAX` ≈ 2730 and `SWING` ≈ 1365, with the average still around 2048.

---

### Stage 3: Making sound with the DAC

*Committed 11:02, `c254cd0`*

**What it does:** sets up the DAC on PA4, which drives the speaker through the board's amplifier. The test plays a 2 kHz beep for 3 seconds, then holds 0.5, 1.0, 1.65, 2.5 and 3.0 V for 3 seconds each, so we can check them with a meter at J1301.

**Why it matters:** this is how audio gets out. On the schematic (sheet 13), the path goes PA4 → J1301 test point → R1304 → R1303 volume knob → 1 µF capacitor → MAX9700 amplifier → speaker. The capacitor only passes changes, which is why a steady voltage is silent and only clicks when it changes. Even with the knob at maximum, only about half the signal reaches the amplifier.

**How the code works:**

```c
GPIOA->CRL &= ~(GPIO_CRL_CNF4_Msk | GPIO_CRL_MODE4_Msk);	// PA4 analog
dac_write(DAC_MID_CODE);									// start at 1.65 V
DAC->CR = DAC_CR_EN1;										// channel 1 on, buffer on
DAC->DHR12R1 = code;										// one write = new voltage
```

Making PA4 "analog" sounds wrong for an output, but the DAC drives the pin through its own analog path, so the pin's digital circuitry has to be disconnected (Board Guide Ch 3). I start the output at 1.65 V, the audio resting level, so the speaker doesn't pop at power-up. The output buffer stays on so the DAC can drive the board's resistor network. According to datasheet Table 63, it can only swing from about 0.2 V to 3.1 V, so codes near 0 or 4095 won't be accurate.

**How the tone is made:** a speaker only makes sound when the voltage moves. So I store one wave of a sine as 32 points and send the next point every 1,125 CPU cycles. That works out as 72,000,000 ÷ (32 points × 2,000 Hz) = 1,125. I picked 2 kHz because the speaker is loudest around 1.7 kHz.

This is also when I moved all the tests into `tests.c`, with `ACTIVE_TEST` in `main.c` picking which one runs. That way, adding a new stage doesn't wipe out the old tests.

---

### Stage 4: External clock interrupt

*Committed 12:07, `9cddcc0`*

**What it does:** makes every rising edge on PF11 (J1101 pin 6) interrupt the chip, whatever else it's doing. That's how the external clock drives sampling. The test counts edges over each half second and shows the clock rate on the LCD. A signal generator stands in for the clock board, which the spec allows for testing as long as it isn't part of the final system.

**Why it matters:** the spec's main requirement is that sampling happens in an interrupt triggered by our external timer. Sampling from the main loop doesn't count, and neither does timing samples off the chip's own timers.

**Why PF11:** it's on J1101, right next to the breadboard, and it's a free pin. It also connects to EXTI line 11, which is the one line that can start an ADC conversion directly in hardware (RM0008 Table 67). We aren't using that yet, but it's a ready upgrade if we ever need less timing jitter.

**How an edge reaches the code:** PF11 → AFIO (picks which port's pin 11 drives line 11) → EXTI (spots the rising edge and sets a pending flag) → NVIC (the CPU's interrupt manager) → our handler.

```c
AFIO->EXTICR[2] = (AFIO->EXTICR[2] & ~AFIO_EXTICR3_EXTI11_Msk) | AFIO_EXTICR3_EXTI11_PF;
EXTI->RTSR |= EXTI_RTSR_TR11;			// rising edges only: one interrupt per clock cycle
EXTI->IMR |= EXTI_IMR_MR11;				// let line 11 through
NVIC_SetPriority(EXTI15_10_IRQn, 0U);	// highest priority, so nothing delays a sample
NVIC_EnableIRQ(EXTI15_10_IRQn);
```

The handler must clear the pending flag. If I forget, it fires again the moment it finishes, forever, and the main loop never runs. Writing a 1 is what clears it.

```c
EXTI->PR = EXTI_PR_PR11;
extclk_edges++;
```

**Things that are easy to get wrong:**

- `EXTICR[2]` is the register the manual calls `AFIO_EXTICR3`, because the array starts at 0.
- Lines 10 to 15 share one handler. That's fine while only line 11 is on, but anyone enabling another line in that range has to check which one fired.
- The Board Guide's `NVIC->ISER[k/32] = (k%32)` is wrong. It should be `1 << (k%32)`, so I use `NVIC_EnableIRQ` instead.
- PF11 uses a pull-down, so an unplugged wire sits low instead of picking up noise and firing random interrupts.
- `extclk_edges` is `volatile`, because the interrupt changes it behind the main loop's back. The main loop can read it safely without turning interrupts off, because a 32-bit read can't be split in half.

**How the rate is measured:** the main loop waits exactly 36,000,000 CPU cycles (half a second), counts the edges, and doubles that. The spec lets us measure with the chip's timing, as long as sampling itself isn't timed by it.

**12:11, include path fix:** Keil 5 couldn't find `core_cm3.h` when building the LCD files. The `LCD` folder has its own copy of `stm32f103xe.h`, which needs `core_cm3.h`, but that file only exists in `Headers`. The old Keil 4 setup found it through a system folder. Adding `.\Headers` to the C/C++ include paths fixed it (`c43a666`).

---

## Thursday 5 November

### Stage 5: Audio passthrough

*Committed 09:21, together with the UI integration*

**What it does:** this is the first real audio path. Sound goes into the jack, gets sampled inside the external clock's interrupt, and comes out of the speaker with no processing, which is exactly what the spec wants on the main page.

**The key decision: never wait for the ADC.** The obvious way to write this is to start a conversion, wait for it, and send the result to the DAC. But the ADC takes 120 of my 180 cycles, so waiting inside the interrupt would leave almost nothing for anything else. Instead, I use the fact that the ADC converts in the background. Each interrupt collects the result from the conversion started on the *previous* edge, then starts the next one and leaves. The cost is two samples of delay: one sample period to convert, and one to come out. That's 5 µs at the top rate and 40 µs at the slowest, which nobody can hear.

**How the code works:** the interrupt handler now lives in `audio.c`, because it does the audio work. On every edge, in this order:

```c
EXTI->PR = EXTCLK_PR_FLAG;		// clear the flag (line 11, defined in extclk.h)
dac_write(next_output);			// output first, at the same time after every edge
if (adc_is_done()) {
	next_output = adc_result();	// collect the last edge's conversion
} else {
	audio_overruns++;			// ADC wasn't finished: the clock is too fast
}
adc_start();					// sample the input for the next edge
if (EXTI->PR & EXTCLK_PR_FLAG) {
	audio_late_edges++;			// the next edge came before we finished
}
```

**Why this order:**

- **Fixed timing:** the DAC write and ADC start always happen at the same point after each edge, no matter what else the interrupt does. When the mixer is added, the processing goes after `adc_start()`, so it can't change when we sample or when we output. Uneven sampling timing acts like noise, which the spec specifically warns about.
- **Same rate in and out:** one DAC write per edge means the output sample rate always equals the input rate, which the spec requires.
- **No processing needed for passthrough:** ADC and DAC both use 12-bit numbers from 0 to 3.3 V, so passing a code straight across keeps the voltage the same. Whatever arrives at PA3 appears at J1301.
- **Overruns:** if the ADC isn't finished, I don't wait. I count it and the DAC repeats the last sample. Waiting would just make the next edge late too.

**How we'll show no dropped or repeated samples:** the spec says we have to convince the marker of this, so there are three independent checks:

1. **`OVERRUNS`:** counts every time a sample would have been repeated because the ADC wasn't done.
2. **`LATE EDGES`:** counts every time a new edge arrived while the interrupt was still busy. The chip can remember one waiting edge, but two would mean one is lost.
3. **The burst test:** the signal generator sends an exact number of pulses, and the edge count has to match exactly. This one catches lost edges that firmware counters can't see, because a lost edge never reaches the code.

**How much CPU it uses:** the test (`ACTIVE_TEST` 6) shows the longest interrupt in cycles and what fraction of the budget that is. The counter can't see the roughly 30 cycles of interrupt entry and exit, so the honest total is `ISR CYCLES` + 30. My estimate was 45 to 60% of the budget at the top rate, but it's only a guess until we read the real number. The spec specifically asks how close we run to the processor's limit, so this number goes in the demo.

---

### Stage 6: SPI commands to the clock board

*SPI module 09:21, `4c4b40f`. Loopback test 09:57, `f8b45f7`. Protocol fix 12:17, `9d2fad5`.*

**What it does:** sends the one-byte commands that set the clock board's rate (`D1`, `D2`, `D4`, `D8` for fx to fx/8) and start or stop it (`E1`, `E0`). The UI's `rate_ctrl.c` decides which byte to send when a button is pressed, and my `clock_spi_send()` in `Sources/spi/clock_spi.c` actually sends it.

**Why it matters:** it's how the touchscreen controls the sample rate, and the demo has to show the rate being changed from the touchscreen.

**How the day went:**

1. **09:21:** first version. It treated PC1 like a normal SPI chip select: high when idle, low while the byte was sent.
2. **09:57:** added a loopback test (`ACTIVE_TEST` 9). With a jumper from J901 pin 5 (MOSI) to pin 3 (MISO), every byte comes straight back, so we could check SPI before the clock board was ready.
3. **The problem:** in test 4 the latch never looked right on the scope. Reading the hardware team's revision 8 notes showed why: their board doesn't use a chip select at all. It wants a separate CMD_LATCH that idles LOW and pulses HIGH after the byte, plus a second strobe called APPLY. Our first version never sent APPLY, so the rate could never have changed.
4. **12:17:** rewritten to match revision 8. CMD_LATCH stayed on PC1 (J901 pin 6), and APPLY went on PF10 (J1101 pin 5). The board doc had put APPLY on J902 pin 6 and the sample clock on J1104 pin 7, but the team moved both onto J1101 so all the clock board wiring sits on one header. The sample clock comes in on J1101 pin 6 (PF11), which is what the firmware already used.

**Why the board needs two strobes:** the board has to make sure that a bad command, or a byte that's still arriving, can never change the rate or cause a glitch. So it splits a command into three steps:

- **The byte shifts in.** It goes into two shift registers on the board, and nothing changes yet.
- **CMD_LATCH copies it into a holding register (U4).** The board's decoder then checks whether it's one of the six valid commands.
- **APPLY lets a valid byte take effect.** The board waits until its own divider is at a safe point in its cycle, then updates. An invalid byte never gets past the decoder, so APPLY does nothing.

**How the code works now:** one full transaction per command, following the board's timing table.

```c
SPI1->DR = cmd;									// 8 bits, MSB first, mode 0, 281 kHz
// ...wait for the byte to finish...
delay_us(GUARD_SHORT_US);
GPIOC->BSRR = 1U << CMD_LATCH_PIN;				// CMD_LATCH HIGH: U4 grabs the byte
delay_us(GUARD_SHORT_US);
GPIOC->BSRR = 1U << (CMD_LATCH_PIN + 16U);		// CMD_LATCH LOW
delay_us(GUARD_SHORT_US);						// decoder settles
APPLY_PORT->BSRR = 1U << APPLY_PIN;				// APPLY HIGH: a valid byte takes effect
delay_us(GUARD_LONG_US);
APPLY_PORT->BSRR = 1U << (APPLY_PIN + 16U);		// APPLY LOW
delay_us(GUARD_LONG_US);						// keep the bus quiet before any more SPI
```

**Why these choices:**

- **Mode 0, MSB first:** the board's 74HC595 shift registers read each bit on the rising clock edge, and the decoder expects the highest bit first.
- **281 kHz (72 MHz ÷ 256):** slow on purpose. Breadboard wires are long and messy, and at this speed the edges have plenty of time to settle. A command is so short that speed doesn't matter.
- **5 µs and 50 µs waits:** the board's table asks for at least 2 µs and 40 µs. I added margin. The audio interrupt can only make these waits longer, never shorter, so the minimums always hold. A whole command takes about 190 µs.
- **Main loop only:** SPI1 is shared with the touchscreen and the EEPROM, and the board's shift registers see every byte on the shared wires. If a touchscreen read happened in the middle of our command, it could overwrite our byte before APPLY. Since the touch driver also only runs in the main loop, and `clock_spi_send()` doesn't return until the whole transaction is done, the two can never overlap.
- **Sharing settings with the touch driver:** the touchscreen uses different SPI settings, so each side saves SPI1's settings, uses its own, and puts the old ones back.
- **Any byte goes out:** the firmware doesn't block invalid bytes. That's deliberate, because the demo has to show the hardware ignoring them.

**Wiring:**

| APE pin | Signal |
|---|---|
| J901 pin 1 / pin 2 | GND / 3.3 V to the clock board (spec: powered only from the APE) |
| J901 pin 4 | SCLK (PA5) |
| J901 pin 5 | MOSI (PA7) |
| J901 pin 6 | CMD_LATCH (PC1) |
| J1101 pin 5 | APPLY (PF10) |
| J1101 pin 6 | Sample clock in (PF11) |

**Testing:**

- **`ACTIVE_TEST` 8:** sends `E1`, `D1`, `D2`, `D4`, `D8`, four bad bytes (`55`, `D3`, `FF`, `00`), `E0`, a bad `E2` and `E1`. After each one it measures the clock coming back on PF11 and lists it on the LCD. With a working clock board, each rate should be half the one before, the bad bytes should change nothing, `E0` should give 0, `E2` should leave it at 0, and the last `E1` should bring back the same rate. That one screen covers three demo requirements: the measured rate at each setting, invalid commands being ignored, and the rate surviving a disable.
- **With only the signal generator:** every line shows the generator's frequency, because it ignores SPI. We can still check the strobes on the scope. Probe SCLK (J1701 pin 5), MOSI (J1701 pin 9), CMD_LATCH (J901 pin 6) and APPLY (J1101 pin 5), triggering on CMD_LATCH rising. You should see 8 clock pulses, then a 5 µs latch pulse, then a 50 µs APPLY pulse, in that order and never overlapping.

**Note on frequency tolerance:** the spec PDF says ±50%, but the hardware team is designing to ±5%, treating ±50% as a typo. Check with a demonstrator which one applies. It doesn't change the firmware either way.

---

## Monday 9 November: where the firmware is up to

**Done in code:** clock tree, ADC, DAC, external clock interrupt, audio passthrough, and SPI commands matching the board's revision 8 protocol.

**`ACTIVE_TEST` numbers** (set at the top of `main.c`):

| Number | Runs | Needs |
|---|---|---|
| 2 | ADC test | Audio into J1302 |
| 3 | DAC test | Meter or scope on J1301 |
| 4 | Full system: touchscreen, audio and clock control | Clock board or generator, audio in |
| 5 | Touch calibration (UI team) | Nothing extra |
| 6 | Audio stats | Clock on J1101 pin 6, audio in |
| 7 | External clock count | Clock on J1101 pin 6 |
| 8 | SPI command test | Clock board, or a generator plus scope |
| 9 | SPI loopback | Jumper J901 pin 5 to pin 3 |

**How the firmware meets the spec so far:**

| Requirement | How |
|---|---|
| Core at 72 MHz from its own clocks | Crystal × 6 through the PLL (Stage 1) |
| Sampling in an ISR triggered by the external timer | The PF11 edge interrupt starts each conversion (Stages 4 and 5) |
| Sample timing not from internal timers | Only the external clock triggers sampling. The cycle counter only measures |
| Output rate equals input rate | One DAC write per clock edge |
| No dropped or repeated samples | `OVERRUNS`, `LATE EDGES` and the burst test |
| Rates set over SPI | `clock_spi_send()`, called from the UI (Stage 6) |
| UI responsive while audio runs | Audio runs in the highest-priority interrupt, and the UI runs in the main loop underneath it |
| Aliasing and reconstruction addressed and measured | **Not done yet** |

**Next for firmware:**

- **Stage 7:** a hook where the mixer processes each sample, safe gain updates from the touchscreen, and capturing samples for the FFT.
- **Stage 8:** collected proof of no dropped or repeated samples, CPU load at each rate, and recovery after a rate change.
- **Still open from the spec:** an input filter against aliasing and an output filter for reconstruction, both with before-and-after measurements. Nobody owns this yet.

---

## Tuesday 10 and Wednesday 11 November: the mixer joins the ISR

*MATLAB design 09:57 10 Nov (eb14b06), mixer code 21:43 10 Nov (d94300f), invalid-command button 22:44 11 Nov (53ae9a8). DSP and UI work, recorded here because it changed `audio.c`.*

The mixer went in through a hook in the ISR: `audio_process`, a function pointer that is 0 for plain passthrough. `Sampling/sampling.c` installs its function there in `ACTIVE_TEST 10`. It sits after `adc_start()`, which is where Stage 5 left room for it, so the mixer can't move the sampling instant or the DAC update. Swapping the pointer is one aligned 32-bit write, so the ISR sees either the old function or the new one, never half of each.

Things I checked from the firmware side:

- **Gains:** each slider writes one 32-bit Q14 value, so the ISR never sees half an update.
- **Coefficients:** two banks. Main fills the spare bank and flips one index, so the ISR never runs half a filter set. They get redesigned when the measured rate drifts more than 1 % from the design rate, which is what I asked for on 9 November.
- **Filter memory:** main can't clear it safely, because the ISR could run halfway through. So main sets a flag and the ISR clears it on its next sample.
- **Rate changes:** the UI switches the mixer off *before* asking for a faster rate, and back on only after fx/8 is running. If it went the other way, the mixer would briefly run at fx and blow the 180-cycle budget.
- **Invalid command:** the `D3 BAD` button sends 0xD3 through `rate_send_invalid()`. Only `LAST CMD` changes on screen. The rate, the enable state and the LEDs stay as they were, because the clock board has to ignore it too.

That covers two of the three Stage 7 jobs, mixer hook and safe gain updates. The FFT capture was still mine to do.

---

## Thursday 12 November

*Written today, not committed yet. It passes a clang syntax check for the Cortex-M3 with `-Wall -Wextra` and no warnings, but it hasn't been built in Keil or run on the board, so there are no measurements below yet.*

### Stage 7: Capturing samples for the FFT

**What it does:** `capture.c` hands the FFT a block of 128 input samples at about 49.9 kHz, at every one of the four rates. The UI and FFT side calls `capture_start(rate)`, waits for `capture_ready()`, reads `capture_data()`, then starts the next block.

**Why decimate:** the spec wants a 128-point FFT showing 0 to 20 kHz at all four rates. At fx, 128 points gives bins 3.1 kHz wide, which is only about 6 bins below 20 kHz. So the ISR adds up 8, 4, 2 or 1 samples per stored point, depending on the rate, which brings every rate down to about fx/8. That gives 390 Hz bins and a 25 kHz top end at every rate.

**How the code works:** in the ISR, after the mixer:

```c
if (capture_armed != 0U) {
	capture_push(code);		// raw input, before the mixer
}
```

`capture_push()` adds the sample to a running sum. Once the sum holds enough samples, it stores one point, scaled to "8 samples' worth" so the numbers look the same at every rate (0 to 32760, with silence at 16384). After point 127 it disarms itself and sets the ready flag.

Why these choices:

- **Armed only when asked:** a whole block takes 2.6 ms, and the display only needs about 30 a second. So most of the time the ISR just does one load and one branch, and the extra work only happens while a block is filling. At fx that matters, because there are only 180 cycles.
- **The measured rate picks the decimation, not the button pressed:** `capture_start()` takes the rate main measured. If the clock board hasn't switched yet, or a signal generator is driving PF11, the block still comes out at about 49.9 kHz.
- **No copying, no double buffer:** once a block is ready, the ISR stops writing until main calls `capture_start()` again. So the FFT can take as long as it likes reading the buffer.
- **Memory barriers:** `__DMB()` before arming, and again before setting the ready flag. Without them the compiler is allowed to move ordinary buffer writes past the `volatile` flag, and main could read a block that isn't finished.
- **The input, not the output:** the spec asks for the spectrum of the input, so the raw ADC code is captured before the mixer touches it.

**The honest limitation:** adding 8 samples together is a cheap low-pass filter, but not a sharp one. At fx it only partly removes 25 to 50 kHz content before it folds down onto the display. That's fine for the speaker band. A proper FIR before decimating wouldn't fit in 180 cycles.

**How we'll check it (ACTIVE_TEST 11):** the test steps through D1, D2, D4 and D8, 4 s each. For each block it shows the measured clock rate, the samples per point, the real capture rate (127 point periods divided by the cycles they took), MIN and MAX, and it draws the block as a trace. What we expect to see:

| Rate | Samples per point | Capture rate |
|---|---|---|
| fx | 8 | about 49.9 kHz |
| fx/2 | 4 | about 49.9 kHz |
| fx/4 | 2 | about 49.9 kHz |
| fx/8 | 1 | about 49.9 kHz |

A steady 1 kHz tone should show the same 2.6 cycles across the trace at all four rates. If the trace gets wider or narrower when the rate changes, the decimation is wrong.

---

### Stage 8: Proving it works at every rate

**What it does:** two tests, which together answer the spec's "convince your marker" requirements: no dropped or repeated samples at any rate, how close we run to the CPU's limit, and recovery after a rate change without a reset.

**ACTIVE_TEST 12, the rate sweep:** this runs the real fx/8 mixer and keeps the FFT capture filling, so it measures the worst case the full system will actually see. For each of D1, D2, D4 and D8 it:

1. Switches the mixer off if the new rate is faster than fx/8, then sends the command.
2. Counts edges in twenty 10 ms windows straight after the command, plus any overruns and late edges in that time (the **switch** counts).
3. At fx/8, switches the mixer on, but only if the measured rate really is below 70.6 kHz. That protects the test when a generator is running fast and ignoring our commands.
4. Runs one steady second with the capture re-arming non-stop. It measures the rate, the longest ISR, and any overruns and late edges (the **run** counts).
5. Works out the settle time: the 10 ms window after which every window stays within 5 % of the steady rate.

It shows three lines per rate:

```
D8  49906 Hz   settled in <=10 ms
  ISR 412 of 1443 cycles = 28% +MIX
  OVR/LATE  switch 0/0  run 0/0
```

(Those numbers only show the layout, nothing has been measured yet.) The ISR figure already has the 30 cycles of interrupt entry and exit added, and it turns red if it reaches the budget.

After the four rates it does the hold check: E0, then D3 while stopped (the rate must stay 0), then E1 (the rate must come back within 2 % of the fx/8 rate), then D3 while running (no change). It prints PASS or FAIL. The bottom lines show totals since power-up, and a green **NO DROPPED OR REPEATED SAMPLES** if this pass added no overruns or late edges at all.

This one screen covers demo items 1 to 3 (all four rates, measured frequency at each, invalid command), the CPU-limit question for the core and Ext A, and recovery after a rate change.

**ACTIVE_TEST 13, the ramp staircase:** this replaces passthrough with a function that ignores the input and steps the DAC up one stair on every edge: 64 stairs of 39 mV, from 0.41 V to 2.85 V, then back to the bottom. With the scope on the clock (J1101 pin 6) and the DAC (J1301), every clock edge should be followed by exactly one stair:

- **A repeated sample** (an overrun) shows up as one stair twice as wide.
- **A lost edge** looks the same on those two channels: a clock edge with no stair after it. Putting PF8 (J1101 pin 3) on a third channel tells them apart. An overrun still has its ISR pulse on PF8, but a lost edge has none.
- **Recovery after a rate change** shows up as the staircase carrying straight on at the new speed, with no gap.

The ramp is fx/64, so 6.2 kHz at fx down to 780 Hz at fx/8. The speaker plays it as a loud buzz, so the volume knob needs to be turned down.

**Why three kinds of proof:** OVERRUNS and LATE EDGES are the firmware checking itself. The staircase is the scope checking the firmware, which a marker can see directly. The burst count from Stage 5 (`TOTAL EDGES` in test 7 against a generator burst of known length) catches edges that never reached the ISR at all. Each one catches a failure the others might miss.

**A note on the ISR's maximum cycle count:** main clears `audio_max_cycles` while the ISR might be updating it. In the worst case, one maximum from just before the clear survives into the next window, so the reading errs high, never low. For a "how close to the limit" number, that's the safe direction.

**What's left for firmware:**

- Build in Keil, then run tests 11, 12 and 13 and put the real numbers in here.
- The FFT itself and the B.2 pitch estimate belong to the UI/FFT side. They only need `capture.h`.
- Still open from the spec, and nobody owns it yet: an analog anti-aliasing filter before the jack and a reconstruction filter after J1301, with before and after measurements. The firmware can't do either one: once the ADC has sampled, any aliasing is already in the signal.

**`ACTIVE_TEST` numbers added since 9 November:**

| Number | Runs | Needs |
|---|---|---|
| 10 | Full system with the 3-band mixer at fx/8 | Clock board, audio in |
| 11 | FFT capture view | Clock board (or generator), tone in |
| 12 | Rate sweep proof | Clock board |
| 13 | Ramp staircase | Clock board (or generator), 2-channel scope, volume low |


---

## What each of you needs from the firmware

**Hardware (clock board):**

- Keep the real fx at or a bit below 399.25 kHz. My estimate is that the firmware stops keeping up somewhere around 450 kHz, and the Stage 5 test will tell us the real limit.
- The clock output needs clean single edges. Ringing on a long wire can look like extra edges, which would show up as double the rate on the LCD.
- The firmware sends exactly the revision 8 transaction, with 5 µs and 50 µs guards.
- Test 8 is the quickest way to check the whole command path.

**DSP (mixer):**

- The spec only requires the mixer at fx/8, which gives 1,443 cycles per sample. After the interrupt's own cost and some room for the touchscreen, I'd budget about 900 cycles for the mixer, but that's an estimate until Stage 5 is measured.
- The mixer will run inside the interrupt, after `adc_start()`, so it has to be fixed-point (no floats) and must never wait for anything.
- Gains come from the touchscreen through a 32-bit value, or a set of coefficients swapped in all at once, so the interrupt never sees half an update.
- Filter coefficients should use the measured sample rate, not the nominal one, because the clock board's oscillator won't sit exactly on 399.25 kHz.

**UI and FFT:**

- Call `rate_set()` and `rate_enable()` as you already do. They send the byte and only return once the whole transaction is done, which takes about 0.2 ms.
- Never call SPI functions from an interrupt.
- The FFT is 128 points at all four rates. At fx, a plain 128-point FFT gives only about 6 bins across 0 to 20 kHz. So the interrupt will average 8, 4, 2 or 1 samples, depending on the rate, to bring everything down to about 49.9 kHz first. That gives 390 Hz bins at every rate.
- The demo wants the display to refresh at least 30 times a second, checked with a GPIO toggle on the scope.
