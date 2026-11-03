#ifndef TESTS_H
#define TESTS_H

// Bench tests for each stage. Neither returns. Both sample or update from the
// main loop, which is fine for testing but is not the real audio path
void test_adc(void);
void test_dac(void);

#endif
