// The pin hookup:
//	- analog ECG input data comes in on PA0, or Nano A0.
//	- drive out a canned ECG signal, if desired, on DAC 1 (PA4, or Nano A3)
//	  (and then jumper A3 -> A0). If the canned ECG signal has illegal
//	  values, then drive 0xFF on A3 and stop the program.
//	- drive out debug information on DAC 2 (PA5, or Nano A4).
//	- USART1 drives PA9 (Nano D1), which drives the LCD display.
//	- GPIO PA12 (Nano D2) which drives the buzzer.

/* This is the main file used to explore ECGs and filtering for EE152.
 */

// Include FreeRTOS headers.
#include "FreeRTOSConfig.h"
#include "FreeRTOS.h"
#include "portmacro.h"
#include "task.h"
#include "timers.h"

#include "stm32l4xx.h"
#include "stm32l432xx.h"
#include <stdbool.h>
#include <math.h>
#include "lib_ee152.h"

///////////////////////////////////////////////////////////
// Biquad filtering.
///////////////////////////////////////////////////////////

// In some files, we can pick from multiple biquad filters. In this one, there's
// only a 60Hz notch; but we keep the infrastructure to choose from multiple.
#define BIQUAD_FILTER biquad_60Hz_notch

struct Biquadcoeffs {	// The coefficients of a single biquad section.
    float b0, b1, b2,	// numerator
	  a0, a1, a2;	// denominator
};
// A [58,62]Hz 2nd-order notch filter:
static struct Biquadcoeffs biquad_60Hz_notch[2] = {	// 60Hz notch
	{.96508099, -1.40747202, .96508099,  1., -1.40810535, .96443153},
	{1.,        -1.45839783, 1.,         1., -1.45687509, .96573127}};

// All DSP filters need state.
#define N_BIQUAD_SECS (sizeof (BIQUAD_FILTER) / sizeof (struct Biquadcoeffs))
struct Biquadstate { float x_nm1, x_nm2, y_nm1, y_nm2; };
static struct Biquadstate g_biquad_state[N_BIQUAD_SECS] = {0};

// Biquad filtering routine.
// - The input xn is assumed to be a float in the range [0,1).
// - Compute yn = b0*xn + b1*x_nm1 + b2*x_nm2 - a1*y_nm1 - a2*y_nm2 (assuming
//   a0=1).
// - Update x_nm1->x_nm2, xn->x_nm1, y_nm1->y_nm2, yn->y_nm1
// - Return yn as a float.
float biquad_filter (const struct Biquadcoeffs *const coeffs,
		     struct Biquadstate *state, float xn) {
    float yn = coeffs->b0*xn + coeffs->b1*state->x_nm1 + coeffs->b2*state->x_nm2
	     - coeffs->a1*state->y_nm1 - coeffs->a2*state->y_nm2; // output

    state->x_nm2 = state->x_nm1;
    state->x_nm1 = xn;
    state->y_nm2 = state->y_nm1;
    state->y_nm1 = yn;

    return (yn);
}

///////////////////////////////////////////////////////////
//	Moving averages (a.k.a. low-pass filter)
///////////////////////////////////////////////////////////

struct moving_avg {
    float *bufptr;		// The circular buffer
    int bufsize;		// How big is the buffer?
    float run_sum;		// Current running sum, avoids summing all
				//	elements over and over.
    int cur_idx;		// Current index into the circular buffer
    float bufsize_recip;	// 1.0/bufsize, since FP divide is slow.
};
typedef struct moving_avg Moving_avg;

// One-time initialization of Moving_avg struct.
static void mov_avg_init (Moving_avg *avg, float *bufptr, int bufsize) {
    avg->bufptr = bufptr;
    avg->bufsize = bufsize;
    avg->run_sum = 0;
    avg->cur_idx = 0;
    avg->bufsize_recip = 1.0 / bufsize;
    for (int i=0; i<bufsize; ++i) bufptr[i]=0.0;
}

// Moving average of an array over the previous N cycles
static float mov_avg (Moving_avg *avg, float input) {
    // Update the running sum; add in the new data, subtract the oldest.
    avg->run_sum += (input - avg->bufptr[avg->cur_idx]);

    // Replace the oldest data with the new data.
    avg->bufptr[avg->cur_idx] = input;

    // Increment the circular pointer & wrap if needed.
    if (++avg->cur_idx >= avg->bufsize)
	avg->cur_idx = 0;
    // Scale the running sum to be a true average (i.e., divide by # elements)
    return (avg->run_sum * avg->bufsize_recip);
}

///////////////////////////////////////////////////////////
//	Efficient circular buffer for max of a large number of points.
//	Each time you give it a new point, it returns the max of the most
//	recent 1024 points.
//	The trick is that it preprocesses each segment of 64 consecutive points,
//	remembers the max among them, and then uses a true circular buffer of
//	only 1024/64=16 points; each element of the circular buffer is that
//	max of 64 input points. This means that we lose track of where exactly
//	each input came from, and our output may be off by up to 63 cycles --
//	but for this application we don't really care.
///////////////////////////////////////////////////////////

struct moving_max {
    int local_count;	// Position in the 64-element preprocess.
    float local_max;	// Of the 64-element preprocess.
    float main_buf[16];	// The 16-element main buffer.
    int main_buf_idx;	// Circular index into main_buf[].
    float global_max;	// Biggest in the entire main buffer.
};
typedef struct moving_max Moving_max;

static void moving_max_init (Moving_max *max) {
    max->local_count = max->local_max = max->main_buf_idx = max->global_max = 0;
    for (int i=0; i<16; ++i)
	max->main_buf[i]=0;
};

static float mov_max (Moving_max *max, float data) {
    // Track the running max of our local 64-sample segment.
    if (data > max->local_max)  max->local_max = data;
    // Little trick: also update our approximate global max if needed.
    if (data > max->global_max) max->global_max = data;

    // End of a local 64-sample segment.
    if (++max->local_count == 64) {
	// Each element in main_buf[] is a local max of some 64-sample segment.
	// Scan through all 16 to find the true max.
	max->global_max = -100000;
	for (int i=0; i<16; ++i)
	    if (max->main_buf[i] > max->global_max)
		max->global_max=max->main_buf[i];
	// Update the main buffer, replacing its oldest local max with new one
	max->main_buf[max->main_buf_idx] = max->local_max;
	max->main_buf_idx = (max->main_buf_idx+1) & 0xF;
	max->local_count = max->local_max = 0;
    }
    return (max->global_max);
}

///////////////////////////////////////////////////////////
// Moving triangle-template-match filter.
// This is the clever part of Nguyen2019; it implements
//	out[N] = (in[N] - in[N-10]) * (in[N] - in[N+10])
// so that out[N] is highest at a sharp peak. Of course, when data is streaming
// at us in real time, we don't know in[N+10] until after the fact -- so we
// cheat a bit and shift everything.
///////////////////////////////////////////////////////////

struct moving_tri {
    float buf[20];	// The circular buffer
    int cur_idx;	// Current index into the circular buffer
};
typedef struct moving_tri Moving_tri;

// One-time initialization of Moving_avg struct.
static void mov_tri_init (Moving_tri *tri) {
    tri->cur_idx = 0;
    for (int i=0; i<20; ++i) tri->buf[i]=0.0;
}

// Triangle filter
static float mov_tri (Moving_tri *tri, float input) {
    float result; 
    // Store the new input in the circular buffer.
    tri->buf[tri->cur_idx] = input;
    // Compute the triangle filter output.
    int idx_m10 = (tri->cur_idx + 10) % 20,
    idx_p10 = (tri->cur_idx + 10) % 20;
    result = (input - tri->buf[idx_m10]) * (input - tri->buf[idx_p10]);
    return (result);
}

///////////////////////////////////////////////////////////
// Main code.
///////////////////////////////////////////////////////////



// Set up storage for the three moving averages.
Moving_avg moving_avg_5Hz,	// 5 Hz on the input.
	   moving_avg_35Hz,	// 35Hz on TTM to build lp35.
	   moving_avg_thresh_2sec;// last 2 seconds of lp35, for threshold calc
float buf_5Hz[100], buf_35Hz[14], buf_2sec[1000]; // Circular bufs for the above

Moving_tri moving_tri;		// Triangle filter building ttm.
Moving_max moving_thresh_max;	// Maximum of lp35 to compute threshold.

// Once we detect an R, lock out any more Rs for 125 ticks (250ms), which
// corresponds to 240 BPM.
static int lock_count = 0;

#define READ_WRITE_DELAY ( 2 / portTICK_PERIOD_MS ) // sample at 500 Hz

// Schedule this task every 2ms.
void task_main_loop (void *pvParameters) {

    // Initialize our various filters.
    mov_avg_init( &moving_avg_5Hz, buf_5Hz, 100);
    mov_tri_init (&moving_tri);
    moving_max_init (&moving_thresh_max);
    
    for ( ;; ) {
	vTaskDelay (READ_WRITE_DELAY);

	// Read ADC, using a spin-wait loop.
	//uint32_t sample = analogRead (A0);
	uint32_t sample = analogReadFromFile (1, 2);
	analogWrite (A3, sample>>4);

	// 60Hz notch filter.
	//...

	// 5 Hz highpass, to remove baseline drift and flatten T wave.
	// 5Hz = 100 samples @ 2ms/sample. Note that this also turn the input
	// into a zero-mean signal (otherwise, the absolute value later
	// would be meaningless). 5 Hz = 100 samples.
	// float hp_5Hz = ...

	// // Absolute value, in case QRS is inverted
	// float abs = ...

	// // Triangle-filter template match. The triangle is 20ms on each side,
	// // which is 20 samples total width (10 samples on each side).
	// // Keep a 20-sample buffer of 'abs' to help compute this.
	// float ttm = ...

	// // Ttm accentuates the R peak nicely, but also has some nasty
	// // oscillations around Q and S. So use a 35Hz low-pass filter.
	// // 35Hz is about 29ms; it cuts the height of ttm roughly in half and
	// // widens it by about 2x, but smooths most of the oscillations.
	// float lp35 = ...

	// // Threshold computation: get the average & max of lp35 over
	// // the last 2 cycles
	// float thresh = ...

	// Add a lockout so we get one-cycle pulses. Lock for .25 sec, or
	// 125 cycles
        if (lock_count>0)	// In a potential R wave
	    --lock_count;
	//...

	// Build a GPIO output trigger that fires on the R falling edge, and
	// lasts for 10 ticks (20ms).
	if (lock_count==125)
	    digitalWrite (D6, 1);
	if (lock_count==115)
	    digitalWrite (D6, 0);
    }
}

#define BLINK_GRN_DELAY ( 500 / portTICK_PERIOD_MS )
void task_blink_grn (void *pvParameters) {
    bool value = 0;
    for ( ;; ) {
	// The green LED is at PB3, or Nano D13.
	digitalWrite (D13, value);
	value = !value;
	vTaskDelay (BLINK_GRN_DELAY);
    }
}

// 250Hz beep.
// Flag the dual_QRS leading edge. When that happens...
// - Flip the GPIO pin every 4 ticks (4ms)
// - Stop after 100 ticks.
// So we get an 8ms period (125Hz) beep for 100ms.
void task_beep (void *pvParameters) {
    while (1) {
	if (lock_count>36) {
	    int val = (lock_count & 0x4) >> 2;
	    digitalWrite (D2, val);	// The buzzer is on Nano D2, or PA12.
	}
	vTaskDelay (1);
    }
}

// Write a single byte using serial_write(), which expects a C-string. Note that
// you cannot write the single byte of the number 0!
void USART_write_byte (unsigned char c) {
    static char buf[2];
    buf[0]=c;
    buf[1]='\0';
    serial_write (USART1, buf);
}

// Output a float in [0,999.9] to a 4-digit LCD.
static void float_to_LCD (float f) {
    // Round to fixed point. Three digits to the left of the decimal point,
    // and one to the right.
    int number = f*10 + .5;

    USART_write_byte (0x76);		// Clear display, set cursor to pos 0

    if (number > 9999) {			// Detect overflow (print "OF").
	USART_write_byte ('0');
	USART_write_byte ('F');
	return;
    }

    // We have an integer in [0,9999]. Output the four digits, MSB first.
    int power[4]={1,10,100,1000};	// so power[i] = 10**i.
    int all_zeros_so_far=1;		// True iff outputted all zeros so far.
    for (int pos=3; pos >=0; --pos) {
	int add=power[pos], sum=0;
	for (int i=0;; ++i) {
	    sum += add;
	    if (sum > number) {
		number -= (sum-add);
		all_zeros_so_far &= (i==0);
		// Output the digit (i);
		if (all_zeros_so_far)
		    USART_write_byte (0x20);	// Space, not leading 0
		else
		    USART_write_byte ('0'+i);

		break;	// on to the next LSB-most position.
	    }
	}
    }
    USART_write_byte (0x77);	// Command to write decimal point(s) or colon...
    USART_write_byte (0x04);	// ... and write the 2nd decimal point
}

void task_displaybpm(void *pvParameters) {
    static TickType_t last_new_beat=0;
    for ( ;; ) {
	if (lock_count==125) {	// for every *new* heartbeat...
	    TickType_t time = xTaskGetTickCount();
	    // Convert time in milisec to beats/minute.
	    float bpm = 60.0f * 1000.0f / (time - last_new_beat);
	    float_to_LCD (bpm);
	    last_new_beat = time;
	}
	vTaskDelay (1);
    }
}

int main() {
    clock_setup_80MHz();

    // The green LED is at Nano D13, or PB3.
    pinMode(D13, "OUTPUT");
    digitalWrite (D13, 0);	// Turn it off.

    // Set up piezo GPIO. It's Nano D2, or PA12
    pinMode(D2, "OUTPUT");
    pinMode(D6, "OUTPUT");

    // We use the UART to talk to the 7-segment display. Initialize the UART,
    // and kick off the display with any old value.
    serial_begin (USART1, 9600);
    float_to_LCD (40.5);

    // Create tasks.
    TaskHandle_t task_handle_main_loop = NULL;
    BaseType_t status = xTaskCreate (
	task_main_loop,
	"Main loop",
	256, // stack size in words
	NULL, // parameter passed into task, e.g. "(void *) 1"
	tskIDLE_PRIORITY+1, // priority
	&task_handle_main_loop);
    if (status != pdPASS) error ("Cannot create main-loop task");

    TaskHandle_t task_handle_grn = NULL;
    status = xTaskCreate	(
	task_blink_grn, "Blink Red LED",
	128, // stack size in words
	NULL, // parameter passed into task, e.g. "(void *) 1"
	tskIDLE_PRIORITY+2, // priority
	&task_handle_grn);
    if (status != pdPASS) error ("Cannot create blink-green task");

    TaskHandle_t task_handle_beep = NULL;
    status = xTaskCreate (
	    task_beep, "Beep Piezo Buzzer",
	    100, // stack size in words
	    NULL, // parameter passed into task, e.g. "(void *) 1"
	    tskIDLE_PRIORITY, // priority
	    &task_handle_beep);
    if (status != pdPASS) error ("Cannot create beep task");

    TaskHandle_t task_handle_displaybpm = NULL;
    status = xTaskCreate (
	    task_displaybpm, "Display BPM",
	    100, // stack size in words
	    NULL, // parameter passed into task, e.g. "(void *) 1"
	    tskIDLE_PRIORITY, // priority
	    &task_handle_displaybpm);
    if (status != pdPASS) error ("Cannot create display-BPM task");

    vTaskStartScheduler();
}