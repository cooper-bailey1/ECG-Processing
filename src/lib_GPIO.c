////////////////////////////////////////////////////////////////////
// GPIO stuff
////////////////////////////////////////////////////////////////////

#include "stm32l432xx.h"
#include "lib_ee152.h"

////////////////////////////////////////////////////////////////////
// Two arrays to map an Arduino Nano pin name (e.g., A0, A1, D0, D1, ...) to
// to the actual STM32L432 GPIO port and pin number. While the Nano name is
// easy to see on the Nucleo 432 board, the latter is what we need for CSR
// programming.
////////////////////////////////////////////////////////////////////

// Given an Arduino Nano pin name, return the STM32L432 GPIO port.
// We typically index this array with an item of "enum Pin" (e.g., A0 or A1).
static GPIO_TypeDef * g_GPIO_port[D13+1] = {
 GPIOA,GPIOA,GPIOA,GPIOA,		// A0=PA0,A1=PA1,A2=PA3,A3=PA4
 GPIOA,GPIOA,GPIOA,GPIOA,		// A4=PA5,A5=PA6,A6=PA7,A7=PA2
 GPIOA,GPIOA,GPIOA,GPIOB,		// D0=PA10,D1=PA9,D2=PA12,D3=PB0
 GPIOB,GPIOB,GPIOB,GPIOC,		// D4=PB7,D5=PB6,D6=PB1,D7=PC14
 GPIOC,GPIOA,GPIOA,GPIOB,		// D8=PC15,D9=PA8,D10=PA11,D11=PB5
 GPIOB,GPIOB				// D12=PB4,D13=PB3.
};

// Given an Arduino Nano pin name, return the STM32L432 GPIO pin.
// So, between this and g_GPIO_port[] above, we can translate an Arduino pin
// name into the chip's actual GPIO port and pin number.
static int g_GPIO_pin[D13+1] = {
 0,1,3,4,		// A0=PA0,A1=PA1,A2=PA3,A3=PA4
 5,6,7,2,		// A4=PA5,A5=PA6,A6=PA7,A7=PA2
 10,9,12,0,		// D0=PA10,D1=PA9,D2=PA12,D3=PB0
 7,6,1,14,		// D4=PB7,D5=PB6,D6=PB1,D7=PC14
 15,8,11,5,		// D8=PC15,D9=PA8,D10=PA11,D11=PB5
 4,3			// D12=PB4,D13=PB3.
};

////////////////////////////////////////////////////////////////////
// Some miscellaneous functions for GPIO programming.
// These are all static, used only in this file.
////////////////////////////////////////////////////////////////////

// Turn on the clock for a GPIO port. 
static void gpio_enable_port (GPIO_TypeDef *gpio) {
    unsigned long field;
    if (gpio==GPIOA)      field=RCC_AHB2ENR_GPIOAEN;
    else if (gpio==GPIOB) field=RCC_AHB2ENR_GPIOBEN;
    else if (gpio==GPIOC) field=RCC_AHB2ENR_GPIOCEN;
    else 		  field=RCC_AHB2ENR_GPIOHEN;
    RCC->AHB2ENR |= field;			// Turn on the GPIO clock
}

// Set a given GPIO pin to be an output (as opposed to an input).
// Also, enable the port's clock.
static void GPIO_set_output (GPIO_TypeDef *gpio,unsigned int pin) {
    gpio_enable_port (gpio);		// Send clocks to this GPIO port.

    // Each GPIO port has a MODER register. It has two bits/pin; each can be
    // one of 00: Input mode, 01: General purpose output mode
    //       10: Alternate function mode, 11: Analog mode (reset state)
    unsigned int b0 = 1<<(pin<<1);	// bit 0 of this pin's two-bit field
    gpio->MODER &= ~(b0<<1);		// Clear the field's bit 1
    gpio->MODER |= b0;			// Set   the field's bit 0
}

static void GPIO_set_input
        (GPIO_TypeDef *gpio, unsigned int pin, unsigned int add_pullup) {
    gpio_enable_port (gpio);		// Send clocks to this GPIO port.
    unsigned int bits = 3<<(pin<<1);	// 1s in this pin's two-bit field.
    gpio->MODER &= ~bits;		// Set two-bit field = 00
    gpio->PUPDR &= ~bits;		// 00 = no pullup or pulldown.
    if (add_pullup)
        gpio->PUPDR |= (1<<(pin<<1));	// Turn on pullup if requested.
}

////////////////////////////////////////////////////////////////////
// A few public functions used only by other libraries for GPIO manipulation.
// They're not really meant to be visible by the end user.
////////////////////////////////////////////////////////////////////

// Set a GPIO pin to be an analog input (mostly for the ADCs)
void GPIO_set_analog_in (GPIO_TypeDef *gpio, unsigned int pin) {
    gpio_enable_port (gpio);		// Send clocks to this GPIO port.

    // GPIO Mode: Input(00), Output(01), AlterFunc(10), Analog(11, reset)
    // Configure PA1. The GPIO Mode Register has two bits/pin.
    GPIOA->MODER |=  3U<<(2*0);		// Mode 11 = Analog

    // GPIO Pullup-pulldown register (PUPDR)
    //	No pull-up, pull-down (00), Pull-up (01), Pull-down (10), Reserved (11)
    // Two bits/pin, and we're setting pins A1 and A2.
    GPIOA->PUPDR &= ~(3U<<(2*0));	// No pull-up, no pull-down

    // GPIO port analog switch control register (ASCR)
    // Enable A1 and A2 as analog input pins.
    // Note we don't have to program ASCR; it doesn't exist on the 432.
    // GPIOA->ASCR |= GPIO_ASCR_EN_1 | GPIO_ASCR_EN_2;
}

// Set a given GPIO pin to be a particular alternate-function.
// It's used by the UART library.
// Params:
//	gpio: which port; one of GPIOA, GPIOB, ... GPIOH.
//	pin:  0-15, for which GPIO pin in the port.
//	func: which of the 15 alternate functions to use.
void set_gpio_alt_func (GPIO_TypeDef *gpio,unsigned int pin,unsigned int func){
    gpio_enable_port (gpio);			// Turn on the GPIO-port clock.

    // Mode Register (MODER). Two bits of mode for each of the 16 pins/port.
    // And 10 -> alternate function.
    gpio->MODER &= ~(3UL << (2*pin));		// Clear the appropriate field.
    gpio->MODER |= 2UL << (2*pin);		// And set to binary 10.

    // AFRL sets the alternate function for pins 0-7; AFRH for pins 8-15.
    // Each register is just eight four-bit fields (one for each pin).
    // The .h file calls the two registers AFR[0] and AFR[1], but then names
    // the bits with the H and L suffixes!
    int idx = (pin>=8);
    int afr_pin = pin - 8*idx;
    gpio->AFR[idx] &= ~(0xFUL << (4*afr_pin));
    gpio->AFR[idx] |=  (func  << (4*afr_pin));

    // Output Speed Register (OSPEEDR). Two bits for each of the 16 pins/port.
    // And 00 -> low speed.
    gpio->OSPEEDR &= ~(3UL<<(2*pin));		// GPIO output speed=slow

    // Pull Up/Pull Down Register (PUPDR). Two bits for each of the 16
    // pins/port. And 00 -> no pullup or pulldown.
    gpio->PUPDR &= ~(3UL <<(2*pin));		// No PUP or PDN

}

////////////////////////////////////////////////////////////////////
// Sort-of-public functions, to be an alternate API to Arduino if you prefer.
////////////////////////////////////////////////////////////////////

// Directly talk to an LED by color, without bothering with the pin name.
// Green is Nano D13, or pin B3.
void init_grn_LED(void) {
    GPIO_set_output (GPIOB, 3);
}

// GPIO port B, pin 3.
// Val is either 0 for off, nonzero for on
void set_grn_LED (int val) {
    if (val)	GPIOB->ODR |= GPIO_ODR_ODR_3;
    else	GPIOB->ODR &= ~GPIO_ODR_ODR_3;
}

void toggle_grn_LED (void) {
    GPIOB->ODR ^= GPIO_ODR_ODR_3;
}

// Set a given GPIO pin to be 1 or 0.
void set_GPIO_pin (GPIO_TypeDef *gpio, int pin, int val) {
    uint32_t shft = 1<<pin;
    if (val)
        gpio->ODR |= shft;
    else
        gpio->ODR &= ~shft;
}

////////////////////////////////////////////////////////////////////
// Finally, the actual Arduino API that's publically visible.
////////////////////////////////////////////////////////////////////

#include <string.h>	// for strcmp().
void pinMode(enum Pin Nano_pin, const char *mode) {
    GPIO_TypeDef *port = g_GPIO_port[Nano_pin];
    int pos = g_GPIO_pin[Nano_pin];

    if (strcmp (mode, "OUTPUT")==0) {
        GPIO_set_output (port, pos);
    } else if (strcmp (mode, "INPUT")==0) {
        GPIO_set_input (port, pos, 0);	// 0 for no pullup.
    } else if (strcmp (mode, "INPUT_PULLUP")==0) {
        GPIO_set_input (port, pos, 1);	// 1 for turn on the input pullup.
    } else error("Bad argument to pinMode()");
}

void digitalWrite (enum Pin pin, bool value) {
    uint32_t shft = 1<<g_GPIO_pin[pin];
    GPIO_TypeDef *gpio = g_GPIO_port[pin];
    if (value)
        gpio->ODR |= shft;
    else
        gpio->ODR &= ~shft;
}

bool digitalRead (enum Pin pin) {
    GPIO_TypeDef *gpio = g_GPIO_port[pin];
    int pos = g_GPIO_pin[pin];
    return ((gpio->IDR >> pos) & 1);	// Pick off the bit from the IDR.
}