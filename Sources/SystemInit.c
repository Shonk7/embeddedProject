#include "../Headers/stm32f103xe.h"
#include "clock.h"

// Spin limit for hardware ready flags. Each pass takes a few cycles on the 8 MHz
// HSI, so this waits tens of ms: far longer than HSE startup or PLL lock (200 us)
#define READY_TIMEOUT	0x10000U

// PLL input x multiplier = 72 MHz. fPLL_IN and PLLMUL are chosen per crystal
#if HSE_HZ == 12000000U
#define PLL_SETTINGS	RCC_CFGR_PLLMULL6
#elif HSE_HZ == 16000000U
#define PLL_SETTINGS	(RCC_CFGR_PLLXTPRE | RCC_CFGR_PLLMULL9)
#else
#error "Add PLL settings for this HSE_HZ"
#endif

// Set by __main, not by SystemInit: see the note above SystemInit()
uint32_t SystemCoreClock = HSI_HZ;

// Returns 1 once (*reg & mask) == value, or 0 if the hardware never got there
static int wait_for(__IO uint32_t *reg, uint32_t mask, uint32_t value) {
	uint32_t i;

	for (i = 0U; i < READY_TIMEOUT; i++) {
		if ((*reg & mask) == value) {
			return 1;
		}
	}
	return 0;
}

// Called by Reset_Handler BEFORE __main, so .data has not been copied and .bss
// has not been zeroed yet. Anything written to a global here gets overwritten,
// so this only touches hardware. main() calls SystemCoreClockUpdate() instead.
void SystemInit(void) {
	// Wait states go up before the clock does. Flash needs 2 above 48 MHz
	// (RM0008 3.3.3). FLASH_ACR_LATENCY_2 is NOT what we want here: in this
	// header it is bit 2, which would mean LATENCY = 4, a reserved value
	FLASH->ACR = FLASH_ACR_PRFTBE | (2U << FLASH_ACR_LATENCY_Pos);

	// Get to a known state. A debugger reset can land here with the PLL still
	// running, and PLLMUL/PLLSRC/PLLXTPRE only accept writes while the PLL is off
	RCC->CR |= RCC_CR_HSION;
	wait_for(&RCC->CR, RCC_CR_HSIRDY, RCC_CR_HSIRDY);
	RCC->CFGR &= ~RCC_CFGR_SW;
	wait_for(&RCC->CFGR, RCC_CFGR_SWS, RCC_CFGR_SWS_HSI);
	RCC->CR &= ~RCC_CR_PLLON;
	wait_for(&RCC->CR, RCC_CR_PLLRDY, 0U);

	// Start the crystal. On failure stay on HSI so that the board still runs;
	// main() sees SystemCoreClock != SYSCLK_HZ and reports the fault
	RCC->CR |= RCC_CR_HSEON;
	if (!wait_for(&RCC->CR, RCC_CR_HSERDY, RCC_CR_HSERDY)) {
		return;
	}

	// All bus prescalers are set while still on 8 MHz, so no bus ever overshoots
	RCC->CFGR = RCC_CFGR_HPRE_DIV1		// AHB  = SYSCLK   = 72 MHz
	          | RCC_CFGR_PPRE2_DIV1		// APB2 = HCLK     = 72 MHz
	          | RCC_CFGR_PPRE1_DIV2		// APB1 = HCLK / 2 = 36 MHz (limit 36)
	          | RCC_CFGR_ADCPRE_DIV6	// ADC  = PCLK2/ 6 = 12 MHz (limit 14)
	          | RCC_CFGR_PLLSRC			// PLL input is HSE, not HSI / 2
	          | PLL_SETTINGS;

	RCC->CR |= RCC_CR_PLLON;
	if (!wait_for(&RCC->CR, RCC_CR_PLLRDY, RCC_CR_PLLRDY)) {
		return;
	}

	// The switch only counts once SWS, the status field, reports it
	RCC->CFGR |= RCC_CFGR_SW_PLL;
	wait_for(&RCC->CFGR, RCC_CFGR_SWS, RCC_CFGR_SWS_PLL);
}

// Works out SYSCLK from what the RCC registers say is running. AHB is always /1
// here, so that is also HCLK. This trusts HSE_HZ: it cannot detect the wrong
// crystal, which is why the Stage 1 test measures a pin as well.
void SystemCoreClockUpdate(void) {
	uint32_t cfgr = RCC->CFGR;
	uint32_t pll_in;
	uint32_t mul;

	switch (cfgr & RCC_CFGR_SWS) {
	case RCC_CFGR_SWS_HSE:
		SystemCoreClock = HSE_HZ;
		break;
	case RCC_CFGR_SWS_PLL:
		if (cfgr & RCC_CFGR_PLLSRC) {
			pll_in = (cfgr & RCC_CFGR_PLLXTPRE) ? (HSE_HZ / 2U) : HSE_HZ;
		} else {
			pll_in = HSI_HZ / 2U;
		}
		// Field value n means x(n + 2), and the two top codes both mean x16
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
