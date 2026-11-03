#ifndef TESTS_H
#define TESTS_H

// One test per stage, picked by ACTIVE_TEST in main.c. None of them return. They
// run from the main loop to check the hardware, so they aren't the real audio path
void test_adc(void);
void test_dac(void);
void test_extclk(void);

#endif
