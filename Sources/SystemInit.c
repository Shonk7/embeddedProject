#include "../Headers/stm32f103xe.h"
#include "clock.h"

// How many times to check a ready flag before giving up. On the 8 MHz boot clock
// this is tens of ms, well past crystal startup or PLL lock (200 us)
#define READY_TIMEOUT	0x10000U

// Get to 72 MHz from either crystal: 12 MHz x 6
#define PLL_SETTINGS	RCC_CFGR_PLLMULL6

// CPU speed in Hz. SystemCoreClockUpdate() fills this in from main
uint32_t SystemCoreClock = HSI_HZ;

// Polls a register until (*reg & mask) == value. Returns 0 on timeout, so a dead
// crystal can't hang the board
static int wait_for(__IO uint32_t *reg, uint32_t mask, uint32_t value) {
	uint32_t i;

	for (i = 0U; i < READY_TIMEOUT; i++) {
		if ((*reg & mask) == value) {
			return 1;
		}
	}
	return 0;
}

// Switches the chip from the 8 MHz boot clock to 72 MHz. Reset_Handler calls this
// before the C runtime starts, so a global written here would get wiped
void SystemInit(void) {
	// Flash needs 2 wait states above 48 MHz, and they must go in before we speed
	// up. The header's FLASH_ACR_LATENCY_2 is bit 2, not "2 wait states"
	FLASH->ACR = FLASH_ACR_PRFTBE | (2U << FLASH_ACR_LATENCY_Pos);

	// Go back to the internal clock and stop the PLL. A debugger reset can land
	// here with the PLL running, and its settings can only change while it's off
	RCC->CR |= RCC_CR_HSION;
	wait_for(&RCC->CR, RCC_CR_HSIRDY, RCC_CR_HSIRDY);
	RCC->CFGR &= ~RCC_CFGR_SW;
	wait_for(&RCC->CFGR, RCC_CFGR_SWS, RCC_CFGR_SWS_HSI);
	RCC->CR &= ~RCC_CR_PLLON;
	wait_for(&RCC->CR, RCC_CR_PLLRDY, 0U);

	// Start the crystal. If it fails, stay on 8 MHz so main can report the fault
	RCC->CR |= RCC_CR_HSEON;
	if (!wait_for(&RCC->CR, RCC_CR_HSERDY, RCC_CR_HSERDY)) {
		return;
	}

	// Set the bus dividers and PLL while still at 8 MHz, so no bus runs too fast
	RCC->CFGR = RCC_CFGR_HPRE_DIV1		// AHB  = SYSCLK   = 72 MHz
	          | RCC_CFGR_PPRE2_DIV1		// APB2 = HCLK     = 72 MHz
	          | RCC_CFGR_PPRE1_DIV2		// APB1 = HCLK / 2 = 36 MHz (limit 36)
	          | RCC_CFGR_ADCPRE_DIV6	// ADC  = PCLK2/ 6 = 12 MHz (limit 14)
	          | RCC_CFGR_PLLSRC			// PLL input is HSE, not HSI / 2
	          | PLL_SETTINGS;

	// Start the PLL and wait for it to lock
	RCC->CR |= RCC_CR_PLLON;
	if (!wait_for(&RCC->CR, RCC_CR_PLLRDY, RCC_CR_PLLRDY)) {
		return;
	}

	// Switch to the PLL. SW is our request, and SWS confirms it happened
	RCC->CFGR |= RCC_CFGR_SW_PLL;
	wait_for(&RCC->CFGR, RCC_CFGR_SWS, RCC_CFGR_SWS_PLL);
}

// Works out the CPU speed from the clock registers. It trusts HSE_HZ, so it can't
// spot the wrong crystal. Only measuring LED0 or a pin can do that
void SystemCoreClockUpdate(void) {
	uint32_t cfgr = RCC->CFGR;
	uint32_t pll_in;
	uint32_t mul;

	// Check which clock is really running
	switch (cfgr & RCC_CFGR_SWS) {
	case RCC_CFGR_SWS_HSE:
		SystemCoreClock = HSE_HZ;
		break;
	case RCC_CFGR_SWS_PLL:
		// PLL input is the crystal (maybe halved) or the internal clock / 2
		if (cfgr & RCC_CFGR_PLLSRC) {
			pll_in = (cfgr & RCC_CFGR_PLLXTPRE) ? (HSE_HZ / 2U) : HSE_HZ;
		} else {
			pll_in = HSI_HZ / 2U;
		}
		// Field value n means multiply by n + 2, capped at 16
		mul = ((cfgr & RCC_CFGR_PLLMULL) >> RCC_CFGR_PLLMULL_Pos) + 2U;
		if (mul > 16U) {
			mul = 16U;
		}
		SystemCoreClock = pll_in * mul;
		break;
	default:
		SystemCoreClock = HSI_HZ;
		break;
	}
}
