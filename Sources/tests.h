#ifndef TESTS_H
#define TESTS_H

// One test per stage, picked by ACTIVE_TEST in main.c. None of them return. The ADC
// and DAC tests run from the main loop, so they aren't the real audio path
void test_adc(void);
void test_dac(void);
void test_extclk(void);
void test_audio(void);
void test_spi(void);

#endif
