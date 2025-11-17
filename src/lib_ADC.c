//**********************************
// ADC
//**********************************

#include "lib_ee152.h"		// for delay_ms().
#include "stm32l432xx.h"
#include <stdint.h>

// g_pin_to_ADC_chan[pin] returns the integer saying which regular ADC channel
// is mapped to 'pin' (which should be of type Enum Pin). We take advantage of
// enum being equivalent to int. Note that we don't handle the special ADC
// external inputs (pins PA11 and PA15 are EXTI 11 and EXTI 15 respectively).
static int g_pin_to_ADC_chan[D13+1] =
{5,6,8,9,		// A0=PA0,A1=PA1,A2=PA3,A3=PA4
 10,11,12,7,		// A4=PA5,A5=PA6,A6=PA7,A7=PA2
 -1,-1,-1,15,		// D0=PA10,D1=PA9,D2=PA12,D3=PB0
 -1,-1,16,-1,		// D4=PB7,D5=PB6,D6=PB1,D7=PC14
 -1,-1,-1,-1,		// D8=PC15,D9=PA8,D10=PA11,D11=PB5
 -1,-1			// D12=PB4,D13=PB3.
};

static void ADC_wakeup (void);
static void ADC_main_config (uint32_t channel);

//****************************************************************************
// Initialize ADC	
// The single master entry point for setting up our ADCs.
// We enable clocks, take the ADCs in/out of reset, turn on the GPIO input,
// Then, set ADC #1 to 12 bits, right-aligned, single (as opposed to multiple
// scanned) conversions of channel #5 (which is hardwired to PA0), single-ended
// input, sample time of .3us, single-shot (not continuous) conversion from a
// software trigger.
// Note that even though we're hardwired to set up ADC channel 5 (PA0),
// functions such as analogRead() can (and do) change it on the fly later.
//****************************************************************************
void ADC_Init (){
    ADC_wakeup();

    // Enable the relevant GPIO pin as an analog input.
    // Usually, we use PA0, which is Nano A0, and is ADC1 input 5.
    // Specifically: turn on the GPIO port clock; then set the pin to be an
    // analog input, with no pull-up or -down, and switch-connected to the ADCs.
    GPIO_set_analog_in (GPIOA, 0);

    ADC_main_config (5);	// Use channel 5, which is PA0 (Nano A0).
}

//**************************************************************************
// ADC wakeup
// The ADC is in deep-power-down mode coming out of reset; its supply is
// internally switched off to reduce the leakage currents. Once we exit
// deep-sleep mode, we can turn on the ADC voltage regulator.
// This is the first function we call from ADC_Init(); after this, we
// do the final programming and finally enable the ADC.
//**************************************************************************
static void ADC_wakeup (void) {
    // According to 16.4.10, you can only write the ADC RCC bits if the ADC
    // is disabled. It's disabled out of reset, so this code is likely unneeded.
    ADC1->CR &= ~ADC_CR_ADEN;  

    // Enable the clock of ADC
    // RCC_AHB2ENR is the AHB2 peripheral-clock-enable register. 
    // ADCEN enables the clock to all ADCs.
    RCC->AHB2ENR  |= RCC_AHB2ENR_ADCEN;

    // RCC_AHB2RSTR is the AHB2 peripheral-reset register.
    // Put the ADC into reset, wait, and take it out, using RCC_AHB2RSTR.ADCRST
    RCC->AHB2RSTR	|=  RCC_AHB2RSTR_ADCRST;	// Go into reset.
    for (volatile int i=0; i<5; ++i)			// Wait till we're
        ;						//   really in reset.
    RCC->AHB2RSTR	&= ~RCC_AHB2RSTR_ADCRST;	// Take out of reset.
    for (volatile int i=0; i<5; ++i)			// Wait till we're
        ;						//   really out of reset

    // Reset leaves us in deep-power-down mode, so exit it now.
    if ((ADC1->CR & ADC_CR_DEEPPWD) == ADC_CR_DEEPPWD)
        ADC1->CR &= ~ADC_CR_DEEPPWD;

    // Enable the ADC internal voltage regulator
    ADC1->CR |= ADC_CR_ADVREGEN;	

    // Wait for ADC voltage regulator start-up time (RM0394 16.4.6). This is
    // T_ADCVREG_STUP, which the 432 datasheet lists as T_ADCVREG_STUP.
    // With an 80MHz clock, that's 20*80 cycles. If our clock is slower, then
    // we wait a bit too long... no prob.
    int wait_time = 1600;	// 20*80.
    while(wait_time != 0)
        wait_time--;
}

// Set to 12 bits, right aligned, 1 conversion only, and specify the channel.
// Set sample time, single-ended and software trigger. Finally, turn on the
// ADC and wait for it.
static void ADC_main_config (uint32_t channel) {
    // System Configuration Controller (SYSCFG) is a set of CSRs controlling
    // numerous system-wide features. We're writing the SYSCFG Configuration
    // Register 1 (CFGR1). The reference manual recommends setting BoostEn=1
    // when the ADC is in low-Vdd_analog mode, allowing the analog input
    // switches to have a lower resistance.
    // I/O analog switches voltage booster
    // The I/O analog switches resistance increases when the VDDA voltage is too
    // low. This requires having the sampling time adapted accordingly (see
    // datasheet for electrical characteristics). This resistance can be
    // minimized at low VDDA by enabling an internal voltage booster with
    // BOOSTEN bit in the SYSCFG_CFGR1 register.
    SYSCFG->CFGR1 |= SYSCFG_CFGR1_BOOSTEN;

    // V_REFINT enable
    // ADC Common Control Register (CCR). One CSR controlling all three
    // ADCs. We're enabling the VrefInt channel; this lets the ADCs'
    // internal reference voltage also drive a dedicated ADC input channel
    // (so you can measure it if you want; it should by definition be the
    // maximum reading).
    ADC1_COMMON->CCR |= ADC_CCR_VREFEN;  

    // ADC Clock Source: System Clock, PLLSAI1, PLLSAI2
    // Maximum ADC Clock: 80 MHz

    // ADC prescaler to select the frequency of the clock to the ADC
    // Same register: don't divide down (i.e., prescale) the ADC clock.
    ADC1_COMMON->CCR &= ~ADC_CCR_PRESC;   // 0000: input ADC clock not divided

    // ADC clock mode
    //   00: CK_ADCx (x=123) (Asynchronous clock mode),
    //   01: HCLK/1 (Synchronous clock mode).
    //   10: HCLK/2 (Synchronous clock mode)
    //   11: HCLK/4 (Synchronous clock mode)	 
    // Same CSR: set CkMode[1:0] to 01, to just use HClk. According to the
    // reference manual, you can only do this if HClk has a 50% duty cycle
    // (i.e., no need to /2 to get 50%), and the AHB clock prescaler is set
    // to 1 (HPRE[3:0] = 0xxx in RCC_CFGR register).
    ADC1_COMMON->CCR &= ~ADC_CCR_CKMODE;  // HCLK = 80MHz
    ADC1_COMMON->CCR |=  ADC_CCR_CKMODE_0;

    //////////////////////////////////////////////////////////////////
    // Independent Mode
    // Same CSR: set the master & slave ADCs to work simultaneously.
    // ADC1 is always the master & ADC2 the slave; they share the same input
    // channels. In regular-simultaneous mode, you just kick off the master
    // and the slave automatically converts at the same time.
    // This is setting ADC_CCR[4:0] = 0b00110, and for some reason doesn't
    // seem to be in stm32l432xx.h (though it was in stm32l476xx.h.
    ADC1_COMMON->CCR &= ~(0x1F);
    ADC1_COMMON->CCR |= 6U;  // 00110: Regular simultaneous mode only

    // ADC configuration reg (one per ADC). Set the resolution to 12 bits.
    // Potential values are (00=12-bit, 01=10-bit, 10=8-bit, 11=6-bit)
    ADC1->CFGR &= ~ADC_CFGR_RES;

    // Data Alignment (0=Right alignment, 1=Left alignment)
    ADC1->CFGR &= ~ADC_CFGR_ALIGN;	// So, right aligned.

    // ADC regular sequence register 1 (ADC_SQR1)
    // L[3:0] tells how many ADC conversions are done (in scan mode) in each
    // scan. The rest of the register (and several others) details the scan
    // sequence.
    // L[3:0]=0000 means just 1 conversion in the regular channel conversion
    // sequence
    ADC1->SQR1 &= ~ADC_SQR1_L;

    // ADC_SQR1 also contains SQ1, which gives the number of the first (and in
    // our case, only) channel to be converted. It's bits[10:6].
    // We set it for ADC1_IN5, which is PA0. But other functions such as
    // analogRead() can later set it on the fly to whichever channel they want.
    ADC1->SQR1 &= ~ADC_SQR1_SQ1;		// Clear bits[10:6]
    ADC1->SQR1 |=  (channel << 6);           	// It's ADC1_IN5, which is PA0.

    // DIFSEL is 1 bit/channel; 1->differential mode, 0->single ended.
    // Again, we hit input #6.
    // Single-ended for ADC12_IN5 (pin PA0).
    uint32_t mask = 1<<channel;
    ADC1->DIFSEL &= ~mask;

    // ADC Sample Time
    // It must be enough for the input voltage source to charge the embedded
    // capacitor to the input voltage level.
    // ADC Sample-time Register 1 (SMPR1) and SMPR2 contain three-bit fields
    // SMP[18:0][2:0]. Each 3-bit field controls the sample time for the
    // corresponding ADC input channel.
    // We set channel #6 to be 24.5 clock cycles.
    // Software may write these bits only when ADSTART=0 and JADSTART=0
    //   000: 2.5 ADC clock cycles      001: 6.5 ADC clock cycles
    //   010: 12.5 ADC clock cycles     011: 24.5 ADC clock cycles
    //   100: 47.5 ADC clock cycles     101: 92.5 ADC clock cycles
    //   110: 247.5 ADC clock cycles    111: 640.5 ADC clock cycles	
    // NOTE: These bits must be written only when ADON=0. 
    ADC1->SMPR1  &= ~ADC_SMPR1_SMP6;      // ADC Sample Time
    ADC1->SMPR1  |= 3U << 18;             // 3: 24.5 ADC clock cycles @80MHz = 0.3 us

    // ADC configuration register; turn off continuous-conversion mode, so
    // we request ADC conversions one at a time.
    ADC1->CFGR &= ~ADC_CFGR_CONT;

    // Configuring the trigger polarity for regular external triggers
    // 00: Hardware Trigger detection disabled, software trig detection enabled
    // 01: Hardware Trigger with detection on the rising edge
    // 10: Hardware Trigger with detection on the falling edge
    // 11: Hardware Trigger with detection on both the rising and falling edges
    // Make sure we only start an ADC from software, rather than any HW trigger.
    ADC1->CFGR &= ~ADC_CFGR_EXTEN; 

    // Enable ADC1, following the procedure in 16.4.9.
    // First clear ADRDY (ADC ready).
    ADC1->ISR &= ~ADC_ISR_ADRDY;

    // Next, enable the ADC.
    ADC1->CR |= ADC_CR_ADEN;  

    // Wait until the hardware says the ADC is really enabled.
    while((ADC1->ISR & ADC_ISR_ADRDY) == 0)
        ;
}

// Read data from ADC #1 using a spin-wait loop. The wait is short, since the HW
// converts one bit/cycle.
uint32_t ADC1_read (void) {
    // Kick off a conversion.
    ADC1->CR |= ADC_CR_ADSTART;

    // Wait until the hardware says it's done.
    // Just as in the 32L476, the common status register (CSR) bit[2] is EOC_MST
    // (end of conversion for the master ADC). But first, stm32l432xx.h doesn't
    // even list ADC1_COMMON->CSR (though the refman says it exists). And then,
    // unsurprisingly, the bit definitions are missing.
    // Luckily, the CSR.EOC_MST bit is just a hardware copy of the ISR.EOC bit.
    // So we'll access that one here.
    //while ( ( ADC1_COMMON->CSR & ADC_CSR_EOC_MST ) == 0 )
    while ((ADC1->ISR & ADC_ISR_EOC) == 0)
        ;
    // And return the data value.
    return (ADC1->DR);
}

// 'pin' can be any input pin that works for an ADC channel.
uint32_t analogRead(enum Pin pin) {
    enum Pin current_pin=D13;		// initialize to an illegal pin.
    static bool ADC_enabled = 0;
    if (!ADC_enabled) {
        ADC_enabled = 1;
        ADC_Init();	// Sets up ADC1 channel 5, which is PA0 (Nano A0).
        delay(20);	// ms; without this, the first read returns 0.
    }
    if (pin != current_pin) {
        current_pin = pin;
        int channel = g_pin_to_ADC_chan[pin];
        if (channel < 0)
            error ("Illegal ADC channel");

        // ADC_SQR1 contains SQ1, which gives the number of the first (and in
        // our case, only) channel to be converted. It's bits[10:6]. We set it
        // here to whatever channel is correct for the user-specified pin.
        ADC1->SQR1 &= ~ADC_SQR1_SQ1;		// Clear bits[10:6]
        ADC1->SQR1 |=  (channel << 6);         	// Set the correct channel.
    }
    return (ADC1_read ());
}