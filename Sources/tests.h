#ifndef TESTS_H
#define TESTS_H

// One test per stage, picked by ACTIVE_TEST in main.c. None of them return. The ADC
// and DAC tests run from the main loop, so they aren't the real audio path
void test_adc(void);
void test_dac(void);
void test_extclk(void);
void test_audio(void);
void test_spi(void);
void test_spi_loopback(void);
void test_capture(void);	// Stage 7: FFT capture blocks at each rate
void test_rates(void);		// Stage 8: rate sweep, settle time, ISR load, OVR/LATE
void test_ramp(void);		// Stage 8: DAC staircase, one stair per clock edge

#endif
