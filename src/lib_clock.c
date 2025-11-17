#include "stm32l432xx.h"
#include "lib_ee152.h"

#ifdef USE_HAL
#include "stm32l4xx_hal.h"
#endif

////////////////////////////////////////////////////////////////////
// Clock stuff
////////////////////////////////////////////////////////////////////

#include <stdint.h>	// to get uint32_t.
#ifdef LL_DEFINES_SYSTEMCORECLOCK
    extern uint32_t SystemCoreClock;
#else
    uint32_t SystemCoreClock = 4000000;
#endif

// Use the high-speed internal clock (16 MHz) as the system clock source SYSCLK.
void clock_setup_16MHz(void) {
    // Clock Control Register (CR), bit 8
    // Enable High Speed Internal Clock (HSI = 16 MHz). This just turns it on,
    // without changing any mux selects.
    RCC->CR |= ((uint32_t)RCC_CR_HSION);

    // Wait until HSI has settled down after turnon. We know this by checking
    // another bit in RCC->CR.
    while ( (RCC->CR & (uint32_t) RCC_CR_HSIRDY) == 0 )
        ;

    // Select line on the main SYSCLK mux, choosing between HSE, MSI, HSI16 and
    // a frequency-multiplied PLL to drive SYSCLK.
    // Note we don't flip the select line until *after* HSI has stabilized :-)
    // And the reset-default value is to use the MSI oscillator.
    RCC->CFGR &= (uint32_t)((uint32_t)~(RCC_CFGR_SW));	// First clear the bits
    RCC->CFGR |= (uint32_t)RCC_CFGR_SW_HSI;		// Then set to 01.

    // Wait till the mux select has actually happened. My guess is there are a
    // few dead cycles to avoid runt clock pulses.
    while ((RCC->CFGR & (uint32_t)RCC_CFGR_SWS) == 0 )
  	;

    // Update the software global variable to talk with other API funcs.
    SystemCoreClock = 16000000;
#ifdef USE_HAL
    HAL_Init();

    // HAL wants to control the SysTick interrupt handler, so as to use it for user-called delay
    // functions. However, we don't use those, and FreeRTOS also wants to own the handler so as
    // to call its scheduler, so our resolution is to let FreeRTOS do it. But both HAL_Init() and
    // SystemClock_Config() enable SysTick interrupts, and FreeRTOS doesn't supply the
    // handler until runtime; so in the interim we make sure to not generate SysTick interrupts.
    // This code must of course come *after* HAL_Init().
    SysTick->CTRL &= ~SysTick_CTRL_TICKINT_Msk;
#endif
}

// Set SYSCLK=80MHz. Set the AHB, APB1 and APB2 prescalers to x1 so that HCLK,
// PCLK2 and PCLK2 are also 80MHz. Set the SAI1 clock = 11.294MHz.
// Don't configure the various other clocks.
void clock_setup_80MHz(void){
    uint32_t HSITrim;

    // To correctly read data from flash memory, the number of wait states
    // must be set based on the frequency of the CPU clock (HCLK) and the
    // supply voltage. According to RM0394 3.3.3, we need 4 wait states at 80MHz
    // (and note that we don't need any wait states at 16MHz).
    FLASH->ACR &= ~FLASH_ACR_LATENCY;
    FLASH->ACR |=  FLASH_ACR_LATENCY_2WS;

    // Clock Control Register (CR), bit 8
    // Enable High Speed Internal Clock (HSI = 16 MHz). This just turns it on,
    // without changing any mux selects.
    RCC->CR |= RCC_CR_HSION;

    // Wait until HSI has settled down after turnon. We know this by checking
    // another bit in RCC->CR.
    while((RCC->CR & RCC_CR_HSIRDY) == 0)
        ;

    // Internal Clock Sources Calibration Register (ICSCR).
    // This 32-bit register has 16 bits each for the HSI and MSI clocks. For
    // each of those, the 16 bits is 8 bits of read-only manufacturing fuses,
    // and 8 bits of user-selected trim values. The manufacturing fuses are
    // pretty good, so we just pick a nice midrange user-trim here.
    HSITrim = 16; // user-programmable trim, added to ICSCR.HSICAL[7:0].
    RCC->ICSCR &= ~RCC_ICSCR_HSITRIM;	// Zero out user trim.
    RCC->ICSCR |= HSITrim << 24;	// And set a midrange value.

    // Turn off the main PLL. It's off at reset anyway, but just be sure in case
    // this function has been called before, or whatever.
    RCC->CR    &= ~RCC_CR_PLLON; 

    // Wait for it to lock. Not sure why this is needed, since it's off!
    while((RCC->CR & RCC_CR_PLLRDY) == RCC_CR_PLLRDY)
        ;

    // Now that the PLL is off, let's configure it.
    // First, select clock source to PLL. The reset value is no clock at all.
    // Now we switch it to HSI (i.e., 16 MHz).
    RCC->PLLCFGR &= ~RCC_PLLCFGR_PLLSRC;	// 00=No clock, 01=MSI,
    RCC->PLLCFGR |= RCC_PLLCFGR_PLLSRC_HSI;	// 10=HSI, 11 = HSE

    // Set the PLL dividers to get 80 MHz output. The final output is
    // 16 MHz * N / (M*R). We'll set N=20, M=2, R=2 to get 16 * 20/4 = 80 MHz.
    //	000: PLLM = 1, 001: PLLM = 2, 010: PLLM = 3, 011: PLLM = 4,
    // Note that N must be in [8,86].
    RCC->PLLCFGR = (RCC->PLLCFGR & ~RCC_PLLCFGR_PLLN) | 20U << 8; // N=20.
    RCC->PLLCFGR = (RCC->PLLCFGR & ~RCC_PLLCFGR_PLLM) | 1U << 4;  // M=2.

    // Set R=2. This is the default, anyway, with bits[26:25]=00.
    RCC->PLLCFGR &= ~RCC_PLLCFGR_PLLR;

    // The PLL is configured; now turn it on. Note that there are two bits to
    // poke, not just one.
    RCC->PLLCFGR |= RCC_PLLCFGR_PLLREN; // Enable Main PLL PLLCLK output 
    RCC->CR   |= RCC_CR_PLLON; 

    // And wait for it to lock.
    while((RCC->CR & RCC_CR_PLLRDY) == 0)
        ;

    // Select line on the main SYSCLK mux, choosing between HSE, MSI, HSI16 and
    // a frequency-multiplied PLL to drive SYSCLK. We haven't touched it so far,
    // and the reset-default value is to use the MSI oscillator. Now we set it
    // to use the PLL that we just configured/locked.
    RCC->CFGR &= ~RCC_CFGR_SW;	  // Turn off all bits, so that we can...
    RCC->CFGR |= RCC_CFGR_SW_PLL; // ...set to the PLL.

    // Wait until the mux has switched
    while ((RCC->CFGR & RCC_CFGR_SWS) != RCC_CFGR_SWS_PLL)
        ;

    // Set the AHB, APB1 and APB2 clock prescalers all to x1 (which is their
    // reset default anyway).
    // AHB controls some peripherals itself, as well as driving APB1 (the
    // low-speed peripheral clock) and APB2 (high-speed peripheral clock) via
    // prescalers.
    RCC->CFGR &= ~RCC_CFGR_HPRE;  // HCLK = SYSCLK x 1
    RCC->CFGR &= ~RCC_CFGR_PPRE1; // APB1 = HCLK x 1
    RCC->CFGR &= ~RCC_CFGR_PPRE2; // APB2 = HCLK x 1

    // Turn off the SA1 PLL
    RCC->CR &= ~RCC_CR_PLLSAI1ON;
    // Then wait for it to actually be off.
    while ( (RCC->CR & RCC_CR_PLLSAI1ON) == RCC_CR_PLLSAI1ON )
        ;

    // SA1 VCO freq = PLL-in-freq * N/M = 16 MHz * 24/2 = 192 MHz
    // We already set the PLL-in-freq=16Mh and M=2 above, for the main PLL.
    // Now set divider SA1N=24.
    RCC->PLLSAI1CFGR &= ~RCC_PLLSAI1CFGR_PLLSAI1N;	// bits[14:8]=0
    RCC->PLLSAI1CFGR |= 24U<<8;				// bits[14:8]=0x18

    // SA1 divider P = 17
    // This sets the SAI1 PLL P output = 192 MHz / 17 = 11.294MHz
    RCC->PLLSAI1CFGR |= RCC_PLLSAI1CFGR_PLLSAI1P;	// bit[17]=1 -> P=17.

    // Enable the SA1 PLL clock output.
    RCC->PLLSAI1CFGR |= RCC_PLLSAI1CFGR_PLLSAI1PEN;	// bit[16]

    // SAI1PLL division factor for PLL48M2CLK (48 MHz clock)
    // RCC->PLLSAI1CFGR &= ~RCC_PLLSAI1CFGR_PLLSAI1Q;
    // RCC->PLLSAI1CFGR |= U<<21;
    // RCC->PLLSAI1CFGR |= RCC_PLLSAI1CFGR_PLLSAI1QEN;

    // PLLSAI1 division factor for PLLADC1CLK (ADC clock)
    // 00: PLLSAI1R = 2, 01: PLLSAI1R = 4, 10: PLLSAI1R = 6, 11: PLLSAI1R = 8
    // RCC->PLLSAI1CFGR &= ~RCC_PLLSAI1CFGR_PLLSAI1R; 
    // RCC->PLLSAI1CFGR |= U<<25;
    // RCC->PLLSAI1CFGR |= RCC_PLLSAI1CFGR_PLLSAI1REN;

    // Turn on the SA1 PLL
    RCC->CR |= RCC_CR_PLLSAI1ON;  // SAI1 PLL enable
    // Then wait for it to actually be on.
    while ( (RCC->CR & RCC_CR_PLLSAI1ON) == 0)
        ;

    // Swing the final mux to drive the SA1 clock. It can come from
    // the SAI1 PLL P output, the SAI2 PLL P output, the main-PLL P output
    // or an external clock. We choose the SAI1-PLL P output.
    RCC->CCIPR &= ~RCC_CCIPR_SAI1SEL;

    // Final clock enable for the SA1 clock.
    RCC->APB2ENR |= RCC_APB2ENR_SAI1EN;

    // Update the software global variable to talk with other API funcs.
    SystemCoreClock = 80000000;
#ifdef USE_HAL
    HAL_Init();

    // HAL wants to control the SysTick interrupt handler, so as to use it for user-called delay
    // functions. However, we don't use those, and FreeRTOS also wants to own the handler so as
    // to call its scheduler, so our resolution is to let FreeRTOS do it. But both HAL_Init() and
    // SystemClock_Config() enable SysTick interrupts, and FreeRTOS doesn't supply the
    // handler until runtime; so in the interim we make sure to not generate SysTick interrupts.
    // This code must of course come *after* HAL_Init().
    SysTick->CTRL &= ~SysTick_CTRL_TICKINT_Msk;
#endif
}

////////////////////////////////////////////////////////////////////
// Spin-loop delay, from the Arduino API.
////////////////////////////////////////////////////////////////////

// This was calibrated against lab_dac.
void delay(unsigned long ms) {
   unsigned int nop_per_ms = (unsigned int) (SystemCoreClock / 9700);
   while (ms-- > 0) {
      volatile unsigned int x=nop_per_ms;
      while (x-- > 0)
         __asm("nop");
   }
}

////////////////////////////////////////////////////////////////////
// Error function. Call it when you hit a fatal error.
////////////////////////////////////////////////////////////////////
void error (char *message) {
    while(1);
}