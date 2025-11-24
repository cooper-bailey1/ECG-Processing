#include "stm32l432xx.h"
#include <stdbool.h>

////////////////////////////////////////////////////////////////////
// Error function. Call it when you hit a fatal error.
////////////////////////////////////////////////////////////////////
void error (char *message);

// The Arduino Nano pin names.
enum Pin {A0,A1,A2,A3,A4,A5,A6,A7,
          D0,D1,D2,D3,D4,D5,D6,D7,D8,D9,D10,D11,D12,D13, NO_PIN };

////////////////////////////////////////////////////////////////////
// Clocks and timing.
////////////////////////////////////////////////////////////////////

// The clock setup from the Zhu book.
// Pick one of these two to run at the beginning of your setup.
void clock_setup_16MHz(void);		// 16 MHz, AHB and APH1/2 prescale=1x
void clock_setup_80MHz(void);		// 80 MHz, AHB and APH1/2 prescale=1x

// Spin-loop time delay
void delay(unsigned long ms);	// From the Arduino API.

/////////////////////////////////////////////
// Analog and digital writing/reading, straight from the Arduino API.
/////////////////////////////////////////////

// pinMode is from the Arduino API. It lets you set the direction of a pin
// as "INPUT", "OUTPUT" or "INPUT_PULLUP".
// A few restrictions on the GPIO pins as per the board solder bridges (SB):
//	- PB3/D13 drives the green LED via solder bridge SB15
//	- PA6/A5 is tied to PB6/D5 via SB16
//	- PA5/A4 (DAC2) is tied to PB7/D4 via SB18
void pinMode (enum Pin pin, const char *mode);
void digitalWrite (enum Pin pin, bool value);
bool digitalRead  (enum Pin pin);

// Analog base routines. Used by the ADC drivers in ADC_DAC.c.
void GPIO_set_analog_in (GPIO_TypeDef *gpio,unsigned int pin);

// Used by UART.c
void set_gpio_alt_func (GPIO_TypeDef *gpio,unsigned int pin,unsigned int func);

//**********************************
// ADC
//**********************************

uint32_t analogRead(enum Pin pin);	// Arduino 12-bit reads.

// And the non-Arduino interface.
void ADC_Init(void);		// Sets up ADC1 channel 5 (Nano A0, PA0).
uint32_t ADC1_read (void);	// 12-bit read of whichever channel is set up.

//**********************************
// DAC
//**********************************

// The Arduino API.
// - 'Pin' can only be Nano A3 (PA4, for DAC1) or Nano A4 (PA5, for DAC2).
//   Note that PA5/A4 is hard tied to PB7/D4 via a solder bridge, so you cannot
//   use D4 as a GPIO pin when you've enabled DAC2.
// - 'Value' is in [0,255] to write in [0,3.3V].
void analogWrite (enum Pin pin, uint32_t value);

// The faster, non-Arduino API.
void DAC1_Init (void);	// Drive PA4, which is also called Nano A3
void DAC2_Init (void);	// Drive PA5, which is also called Nano A4

// Write 12-bit unsigned data to DAC 1, which drives PA4, a.k.a. Nano A3
void DAC1_write (uint32_t data);

// Write 12-bit unsigned data to DAC 2, which drives pin PA5, a.k.a., Nano A4
void DAC2_write (uint32_t data);

//**********************************
// UART
//**********************************

// Initialization.
void serial_begin (USART_TypeDef *USARTx, int baud_rate);

// Very basic function: send a character string to the UART, one byte at a time.
// Spin wait after each byte until the UART is ready for the next byte.
void serial_write (USART_TypeDef *USARTx, const char *buffer);

// Spin wait until we have a byte.
char serial_read (USART_TypeDef *USARTx);

// Public function used to send binary data to the host-scope program(s).
void UART_send_to_host (float ch1, float ch2);

// Check if there are any keystrokes ready to read.
bool UART_has_input_data();

//**********************************
// Debug
//**********************************
void analogWriteFromFile (enum Pin pin);
int analogReadFromFile (int channel, int n_channels);