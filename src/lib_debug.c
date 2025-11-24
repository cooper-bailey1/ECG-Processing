#include "lib_ee152.h"		// for ???
#include "stm32l432xx.h"
#include <stdint.h>
////////////////////////////////////////////////////
// This portion of the file contains debug-related functions.
//    -	analogWriteDbg (pin, val, offset, shift_rt_amt, when_to_start):
//	Similar to analogWrite(), but tweaked to be good at writing an integer
//	out to the scope to examine it even when you're not sure what the
//	integer's min/max range is (and remember, the DAC only does [0,0xFF]).
//    -	analogWriteFromFile (pin):
//	Just like analogWrite(), but it doesn't take a parameter for the value
//	to write. Instead, you define ANALOG_WRITE_FILENAME and it takes
//	integers from that file. When it gets to the end it loops back around.
//	If you don't define ANALOG_WRITE_FILENAME, then this function won't
//	even exist.
//    - analogReadFromFile (). Like analogRead(), but it doesn't take a 'pin'
//	parameter. Instead, you define ANALOG_READ_FILENAME and it returns
//	successive integers from that file. When it gets to the end, it loops
//	back around again.
//	The integers are assumed to be in [0,2**12); we shift them >>4.
//	If you don't define ANALOG_READ_FILENAME, then this function won't
//	even exist.
////////////////////////////////////////////////////

/* A version of analogWrite() that's aimed at throwing a bug-related signal out
 * to a scope to help debug it. It has some special sauce:
 * - often, you're never sure of the min/max range of a signal; this function
 *   keeps running min/max in globals that you can examine in a debugger. It
 *   even lets you delay the start of the tracking for signals with some startup
 *   error.
 * - lets you apply offset/scaling to keep the signal within the DAC's [0,0xFF]
 *   range. And pegs the output to [0,0xFF] to keep the scope more readable
 *   anyway.
 */
int analogWriteDbg_A3_min_val=100000, analogWriteDbg_A3_max_val=-100000,
    analogWriteDbg_A4_min_val=100000, analogWriteDbg_A4_max_val=-100000;
void analogWriteDbg (enum Pin pin, int val, int offset,
                     int shift_rt_amt, int when_to_start) {
    static int cycle_count = 0;
    if (++cycle_count > when_to_start) {
        if (pin==A3) {
            if (val>analogWriteDbg_A3_max_val) analogWriteDbg_A3_max_val=val;
            if (val<analogWriteDbg_A3_min_val) analogWriteDbg_A3_min_val=val;
        } else if (pin==A4) {
            if (val>analogWriteDbg_A4_max_val) analogWriteDbg_A4_max_val=val;
            if (val<analogWriteDbg_A4_min_val) analogWriteDbg_A4_min_val=val;
        }
    }

    int adjusted = (val + offset);
    if (shift_rt_amt > 0) adjusted >>= shift_rt_amt;
    if (shift_rt_amt < 0) adjusted <<= (-shift_rt_amt);
    if (adjusted > 0xFF) adjusted=0xFF;
    if (adjusted < 0)    adjusted=0;
    analogWrite (pin, adjusted);
}


// analogWriteFromFile().
//	Just like analogWrite(), but it doesn't take a parameter for the value
//	to write. Instead, you define ANALOG_WRITE_FILENAME and it takes
//	integers from that file. When it gets to the end it loops back around.
#ifdef ANALOG_WRITE_FILENAME
static unsigned short int analogWrite_data[] = {
#include ANALOG_WRITE_FILENAME
};

void analogWriteFromFile (enum Pin pin) {
    int n_datapoints = (sizeof analogWrite_data) / (sizeof (short int));
    static int i=0;
    unsigned int data = analogWrite_data[i] >> 4;
    if (data > 0xFF) {
        analogWrite (pin, 0xFF);
        error ("Canned data is out of range");
    } else
    analogWrite (pin, data);
    if (++i == n_datapoints)
        i = 0;
}
#endif

// analogReadFromFile (). Like analogRead(), but it doesn't take a 'pin'
//	parameter. Instead, you define ANALOG_READ_FILENAME and it returns
//	successive integers from that file. When it gets to the end, it loops
//	back around again.
#define ANALOG_READ_FILENAME "kalan_canned_ecg.txt"
static unsigned short int analogRead_data[] = {
#include ANALOG_READ_FILENAME
};
static const int n_datapoints = (sizeof analogRead_data) / (sizeof (short int));

// Returns 12-bit data.
// Channel number must be in [0,n_channels).
int analogReadFromFile (int channel, int n_channels) {
    static int idx_ch0=0, idx_ch1=1;
    int *ip = (channel==0? &idx_ch0:&idx_ch1);
    unsigned int data = analogRead_data[*ip];
    *ip += n_channels;
    if (*ip >= n_datapoints)
        *ip -= n_datapoints;

    if ((data > 0xFFF) || (data < 0))
        error ("Out-of-range integer value in analogReadFromFile()");	
    return (data);
}
