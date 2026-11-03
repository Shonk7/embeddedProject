#ifndef TESTS_H
#define TESTS_H

// Bench tests for each stage. None of them return. The ADC and DAC tests run from
// the main loop, which is fine for testing but is not the real audio path
void test_adc(void);
void test_dac(void);
void test_extclk(void);

#endif
