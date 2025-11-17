#include "stm32l432xx.h"
#include "lib_ee152.h"
#include <stdbool.h>

static void USART_Init (USART_TypeDef *USARTx, bool tx_en, bool rx_en,int baud);
static void UART1_GPIO_Init(void);
static void UART2_GPIO_Init(void);
static void USART_Delay(uint32_t us);

// These used to be public functions, but are now replaced with the Arduino API.
// They're now what the Arduino API is built out of, so they're static.
// A few other functions (UART_write() and UART_write_byte_nonB()) are here but
// commented out, since the Arduino API doesn't use them.
static void UART_write_byte(USART_TypeDef *USARTx, char data);
static void UART1_Init (int baud);
static void UART2_Init (int baud);

static void UART2_Init (int baud) {
    // Enable the clock of USART 1 & 2
    RCC->APB1ENR1 |= RCC_APB1ENR1_USART2EN;  // Enable USART 2 clock

    // Select SYSCLK as the USART2 clock source. The reset default is PCLK1;
    // we usually set both SYSCLK and PCLK1 to 80MHz anyway.
    RCC->CCIPR &= ~RCC_CCIPR_USART2SEL;
    RCC->CCIPR |=  RCC_CCIPR_USART2SEL_0;

    UART2_GPIO_Init();
    USART_Init (USART2, 1, 1, baud);	// Enable both Tx and Rx sides.
}

// The Nucleo 432 wires PA2 to the ST-Link's VCP_TX pin via AF7, and PA15 to
// VCP_RX via AF3.
// In this function, we set up those pins.
// The alternate-function designation presumably sets most on the GPIO pin's
// internals. However, we still set them here to high-speed, pullup-only,
// push-pull drive.
static void UART2_GPIO_Init(void) {
    set_gpio_alt_func (GPIOA,  2, 7);
    set_gpio_alt_func (GPIOA, 15, 3);

    // Set PA2 and PA15 to very-high-speed. This changes the output slew rate.
    GPIOA->OSPEEDR |=   0x3<<(2*2) | 0x3<<(2*15);

    // Both PA2 and PA15 are in pullup/down mode 01, which means pull-up only.
    // This is arguably not needed. During normal operation, we're doing push-
    // pull drive and so don't need a pullup or pulldown. Some people like a
    // pullup to stop the data line from bouncing during reset before any MCU
    // drives it -- but our pullup won't turn on until this code runs, anyway!
    GPIOA->PUPDR   &= ~((0x3<<(2*2)) | (0x3<<(2*15)));	// Clear bits
    GPIOA->PUPDR   |=   (0x1<<(2*2)) | (0x1<<(2*15));	// Set each to 01.

    // Both PA2 and PA15 are push-pull (which is the reset default, anyway).
    GPIOA->OTYPER  &= ~((0x3<<(2*2)) | (0x3<<(2*15)));	// Clear bits
}

static void UART1_Init (int baud) {
    // Enable the clock of USART 1 & 2
    RCC->APB2ENR |= RCC_APB2ENR_USART1EN;  // Enable USART 1 clock

    // Select SYSCLK as the USART1 clock source. The reset default is PCLK1;
    // we usually set both SYSCLK and PCLK1 to 80MHz anyway.
    RCC->CCIPR &= ~RCC_CCIPR_USART1SEL;
    RCC->CCIPR |=  RCC_CCIPR_USART1SEL_0;

    UART1_GPIO_Init();
    USART_Init (USART1, 1, 0, baud);	// Enable just the Tx side.
}

// Like UART2_GPIO_Init, but just use PA9 in AF7 mode as the Tx data.
static void UART1_GPIO_Init(void) {
    set_gpio_alt_func (GPIOA,  9, 7);

    // Set PA9 to very-high-speed. This changes the output slew rate.
    GPIOA->OSPEEDR |=   0x3<<(2*9);

    // Set PA9 in pullup/down mode 01, which means pull-up only.
    GPIOA->PUPDR   &= ~(0x3<<(2*9));	// Clear bits
    GPIOA->PUPDR   |=  (0x1<<(2*9));	// Set to 01.

    // Set PA9 as push-pull (which is the reset default, anyway).
    GPIOA->OTYPER  &= ~(0x3<<(2*9));	// Clear bits
}

// Set for 8 data bits, 1 start & 1 stop bit, 16x oversampling, 9600 baud.
// And by default, we also get no parity, no hardware flow control (USART_CR3),
// asynch mode (USART_CR2).
static void USART_Init (USART_TypeDef *USARTx, bool tx_en, bool rx_en,int baud){
    // Disable the USART.
    USARTx->CR1 &= ~USART_CR1_UE;  // Disable USART

    // The "M" field is two bits, M1 and M0. We're setting it to 00 (which
    // is the reset value anyway), to use 8-bit words and one start bit.
    USARTx->CR1 &= ~USART_CR1_M;

    // Configure stop bits to 1 stop bit (which is the default). Other
    // choices are .5, 1.5 and 2 stop bits.
    USARTx->CR2 &= ~USART_CR2_STOP;   

    // Set baudrate as desired. This is done by dividing down the APB1 clock.
    // E.g., 80MHz/9600 = 8333 = 0x208D.
    // (We're oversampling by 16; the calculation would be slightly
    // different if we were 8x mode).
    extern uint32_t SystemCoreClock;
    uint32_t val = SystemCoreClock / baud;
    USARTx->BRR  = val;

    // Configure oversampling mode: Oversampling by 16 (which is the
    // default). This means that our Rx runs at 16x the nominal baud rate.
    // If we're not enabling the Rx anyway, this step is moot (but harmless).
    USARTx->CR1 &= ~USART_CR1_OVER8;

    // Turn on transmitter and receiver enables. Note that the entire USART
    // is still disabled, though. Turning on the Rx enable kicks off the Rx
    // looking for a stop bit.
    if (tx_en)
        USARTx->CR1  |= USART_CR1_TE;
    if (rx_en)
        USARTx->CR1  |= USART_CR1_RE;

    // We originally turned off the USART -- now turn it back on.
    // Note that page 1202 says to turn this on *before* asserting TE and/or RE.
    USARTx->CR1  |= USART_CR1_UE; // USART enable                 

    // Verify that the USART is ready to transmit...
    if (tx_en)
        while ( (USARTx->ISR & USART_ISR_TEACK) == 0)
            ;
    // ... and to receive.
    if (rx_en)
        while ( (USARTx->ISR & USART_ISR_REACK) == 0)
            ;
}


// Very basic function: just spin wait, checking the data-available bit until
// we have data. Then return one byte.
char UART_read (USART_TypeDef * USARTx) {
    // The SR_RXNE (Read data register not empty) bit is set by hardware.
    // We spin wait until that bit is set
    while (!(USARTx->ISR & USART_ISR_RXNE))
        ;

    // Reading USART_DR automatically clears the RXNE flag 
    return ((char)(USARTx->RDR & 0xFF));
}

// Very basic function: just give nBytes bytes to the UART, one byte at a time.
// Spin wait after each byte until the UART is ready for the next byte.
/*static void UART_write(USART_TypeDef *USARTx, const char *buffer) {
    // The main flag we use is Tx Empty (TXE). The HW sets it when the
    // transmit data register (TDR) is ready for more data. TXE is then
    // cleared when we write new data in (by a write to the USART_DR reg).
    // When the HW transfers the TDR into the shift register, it sets TXE=1.
    for (unsigned int i = 0; buffer[i] != '\0'; i++)
        UART_write_byte (USARTx, buffer[i]);

    // RM0394 page 1203 says that you must wait for ISR.TC=1 before you shut
    // off the USART. We wait over here... not needed for us, since we never
    // shut off the USART anyway.
    while (!(USARTx->ISR & USART_ISR_TC));
    USARTx->ISR &= ~USART_ISR_TC;
}*/

static void UART_write_byte (USART_TypeDef *USARTx, char data) {
    // spin-wait until the TXE (TX empty) bit is set
    while (!(USARTx->ISR & USART_ISR_TXE));

    // Writing USART data register automatically clears the TXE flag
    USARTx->TDR = data & 0xFF;

    // Wait 300us or so, to let the HW clear TXE.
    USART_Delay (300);
}

/* This one uses vTaskDelay instead of spin-waiting.
static void UART_write_byte_nonB (USART_TypeDef *USARTx, uint8_t data) {
    // spin-wait until TXE (TX empty) bit is set
    while (!(USARTx->ISR & USART_ISR_TXE))
        vTaskDelay (1);

    // Writing USART_DR automatically clears the TXE flag
    USARTx->TDR = data & 0xFF;

    // Wait 300us or so, to let the HW clear TXE.
    USART_Delay (300);
}*/

// Assume that each usec of delay is about 13 times around the NOP loop.
// That's probably about right at 80 MHz (maybe a bit too slow).
static void USART_Delay(uint32_t us) {
    uint32_t time = 100*us/7;    
    while(--time);   
}

/////////////////////////////////////////////
// Non-Arduino-like useful public functions.
/////////////////////////////////////////////

// Send data to a host in the format used by scope_host programs.
// The data is two floats in the range [0,1).
// The format is to pack them into a 3B packet.
#define BITS(n,h,l) ((n>>l) & ((1<<(h+1-l))-1))
void UART_send_to_host (float ch1, float ch2) {
    ch1=(ch1<0)? 0:ch1; ch1=(ch1>1)?1:ch1;
    ch2=(ch2<0)? 0:ch2; ch2=(ch2>1)?1:ch2;

    unsigned int i1=ch1*2047,	// Turn ch1 into an 11-bit fixed-point binary,
		 i2=ch2*1023,	// and ch2 into 10 bit.
		 packed_3B = (i1<<10) | i2,
		 b0=0x80 | BITS(packed_3B,20,14),
		 b1= BITS(packed_3B,13,7),
		 b2= BITS(packed_3B,6,0);

    // We cannot use serial_write() to send this data; it takes a C-style
    // (i.e., zero-terminated) string, which means we cannot send any
    // actual zero characters!
    UART_write_byte (USART2, b0);
    UART_write_byte (USART2, b1);
    UART_write_byte (USART2, b2);
}

bool UART_has_input_data(USART_TypeDef *USARTx) {
    // The SR_RXNE (Read data register not empty) bit is set by hardware.
    // We spin wait until that bit is set
    bool has_data = USARTx->ISR & USART_ISR_RXNE;
    return (has_data);
}

/////////////////////////////////////////////
// Arduino-like interface
/////////////////////////////////////////////

// Very basic function: send a character string to the UART, one byte at a time.
// Spin wait after each byte until the UART is ready for the next byte.
void serial_write (USART_TypeDef *USARTx, const char *buffer) {
    // The main flag we use is Tx Empty (TXE). The HW sets it when the
    // transmit data register (TDR) is ready for more data. TXE is then
    // cleared when we write new data in (by a write to the USART_DR reg).
    // When the HW transfers the TDR into the shift register, it sets TXE=1.
    for (unsigned int i = 0; buffer[i] != '\0'; i++)
        UART_write_byte (USARTx, buffer[i]);

    // RM0394 page 1203 says that you must wait for ISR.TC=1 before you shut
    // off the USART. We never shut off the USART... but we'll wait anyway.
    while (!(USARTx->ISR & USART_ISR_TC));
    USARTx->ISR &= ~USART_ISR_TC;
}

// Note that the official Arduino spec does *not* take a baud-rate parameter.
// But it's just so much more convenient this way :-)
// Why isn't it an optional parameter defaulting to 9600? Because C doesn't
// support default parameters.
void serial_begin (USART_TypeDef *USARTx, int baud_rate) {
    if (USARTx == USART1)
        UART1_Init (baud_rate);
    else if (USARTx == USART2)
        UART2_Init (baud_rate);
    else
        error ("Initializing an illegal UART");
}

// Waits for a character to be typed, and then reads it. We block in a spin
// loop until the character is typed.
char serial_read (USART_TypeDef *USARTx) {
    // The SR_RXNE (Read data register not empty) bit is set by hardware.
    // We spin wait until that bit is set
    while (!(USARTx->ISR & USART_ISR_RXNE))
        ;

    // Reading USART_DR automatically clears the RXNE flag 
    return ((char)(USARTx->RDR & 0xFF));
}