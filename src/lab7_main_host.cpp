// /* This is the main file used to explore ECGs and filtering for EE152.
//  * It matches ECG_triangle2019.py in
//  *	Dropbox/EE152_embedded/code_filtering_python.
//  */

// #include <iostream>
// #include <sstream>
// #include <fstream>
// #include <vector>
// #include <stdlib.h>
// #include "stdint.h"
// #include <cmath>

// #define F_SAMP 500

// // My own function for printing -- feel free to remove it.
// using namespace std;
// #define LOG(args) cout << args << endl
// #define DIE(args) { cout << args << endl; exit(0); }

// ///////////////////////////////////////////////////////////
// //	Read ECG data from a file. Then return it number by
// //	number to mimic analogRead().
// ///////////////////////////////////////////////////////////

// // Globals to hold the input ECG data that we read from a file, and then
// // return it number by number to mimic analogRead().
// static vector<int> g_numbers;
// static int g_index=0;  
// static int end_segment_RMS=0;	// for plotting communication

// // Read the given 'filename', grab the integers and store them into g_numbers.
// // Ignore any commas or other whitespace. A single-lead ECG has one reading,
// // resulting in one number (called a channel) on each line. A six-lead ECG has
// // two simultaneous readings, resulting in two comma-separated numbers (two
// // channels) on each line. In that case, we only look at one of them.
// static void analogRead_start(string filename, int channel=0) {
//     ifstream in_file (filename);
//     if (!in_file.is_open())
// 	DIE ("Cannot open "<<filename);

//     string line; int pos;
//     while (getline(in_file, line)) {
// 	// Replace all commas and tabs with spaces.
// 	while ((pos = line.find_first_of (",\t")) != string::npos)
// 	    line[pos] = ' ';

// 	// Grab all of the integers on this line, pick one channel, and
//         // push it onto g_numbers.
// 	vector<int> numbs;
// 	istringstream iss(line);
// 	int val;
// 	while (iss >> val) {
// 	    //LOG ("Value *"<<val<<"*");
// 	    numbs.push_back (val);
// 	}
// 	g_numbers.push_back(numbs[channel]);	// Six-lead ECG has two channels
//     }
//     //LOG ("Parsed " << g_numbers.size() << " values from "<<filename);
//     //LOG ("First values are "<<g_numbers[0]<<", "<<g_numbers[1]);
//     in_file.close();
// }

// // A replacement for analogRead(). Make sure to call analogRead_start() first.
// // It returns each successive value in turn. When it uses them all, then it
// // returns -1. If you keep calling it, it will then loop around again.
// static int analogRead() {
//     if (g_index >= g_numbers.size()) {
// 	g_index = 0;
//     }
//     //LOG ("analogRead() returning "<< g_numbers[g_index]);
//     return (g_numbers[g_index++]);
// }


// ///////////////////////////////////////////////////////////
// // Biquad filtering.
// ///////////////////////////////////////////////////////////

// // In some files, we can pick from multiple biquad filters. In this one, there's
// // only a 60Hz notch; but we keep the infrastructure to choose from multiple.
// #define BIQUAD_FILTER biquad_60Hz_notch

// struct Biquadcoeffs {	// The coefficients of a single biquad section.
//     float b0, b1, b2,	// numerator
// 	  a0, a1, a2;	// denominator
// };
// // A [58,62]Hz 2nd-order notch filter:
// static struct Biquadcoeffs biquad_60Hz_notch[2] = {	// 60Hz notch
// 	{.96508099, -1.40747202, .96508099,  1., -1.40810535, .96443153},
// 	{1.,        -1.45839783, 1.,         1., -1.45687509, .96573127}};

// // All DSP filters need state.
// #define N_BIQUAD_SECS (sizeof (BIQUAD_FILTER) / sizeof (struct Biquadcoeffs))
// struct Biquadstate { float x_nm1, x_nm2, y_nm1, y_nm2; };
// static struct Biquadstate g_biquad_state[N_BIQUAD_SECS] = {0};

// // Biquad filtering routine.
// // - The input xn is assumed to be a float in the range [0,1).
// // - Compute yn = b0*xn + b1*x_nm1 + b2*x_nm2 - a1*y_nm1 - a2*y_nm2 (assuming
// //   a0=1).
// // - Update x_nm1->x_nm2, xn->x_nm1, y_nm1->y_nm2, yn->y_nm1
// // - Return yn as a float.
// float biquad_filter (const struct Biquadcoeffs *const coeffs,
// 		     struct Biquadstate *state, float xn) {
//     float yn = coeffs->b0*xn + coeffs->b1*state->x_nm1 + coeffs->b2*state->x_nm2
// 	     - coeffs->a1*state->y_nm1 - coeffs->a2*state->y_nm2; // output

//     state->x_nm2 = state->x_nm1;
//     state->x_nm1 = xn;
//     state->y_nm2 = state->y_nm1;
//     state->y_nm1 = yn;

//     return (yn);
// }

// ///////////////////////////////////////////////////////////
// //	Moving averages (a.k.a. low-pass filter)
// ///////////////////////////////////////////////////////////

// struct moving_avg {
//     float *bufptr;		// The circular buffer
//     int bufsize;		// How big is the buffer?
//     float run_sum;		// Current running sum, avoids summing all
// 				//	elements over and over.
//     int cur_idx;		// Current index into the circular buffer
//     float bufsize_recip;	// 1.0/bufsize, since FP divide is slow.
// };
// typedef struct moving_avg Moving_avg;

// // One-time initialization of Moving_avg struct.
// static void mov_avg_init (Moving_avg *avg, float *bufptr, int bufsize) {
//     avg->bufptr = bufptr;
//     avg->bufsize = bufsize;
//     avg->run_sum = 0;
//     avg->cur_idx = 0;
//     avg->bufsize_recip = 1.0 / bufsize;
//     for (int i=0; i<bufsize; ++i) bufptr[i]=0.0;
// }

// // Moving average of an array over the previous N cycles
// static float mov_avg (Moving_avg *avg, float input) {
//     // Update the running sum; add in the new data, subtract the oldest.
//     avg->run_sum += (input - avg->bufptr[avg->cur_idx]);

//     // Replace the oldest data with the new data.
//     avg->bufptr[avg->cur_idx] = input;

//     // Increment the circular pointer & wrap if needed.
//     if (++avg->cur_idx >= avg->bufsize)
// 	avg->cur_idx = 0;
//     // Scale the running sum to be a true average (i.e., divide by # elements)
//     return (avg->run_sum * avg->bufsize_recip);
// }

// ///////////////////////////////////////////////////////////
// //	Efficient circular buffer for max of a large number of points.
// //	Each time you give it a new point, it returns the max of the most
// //	recent 1024 points.
// //	The trick is that it preprocesses each segment of 64 consecutive points,
// //	remembers the max among them, and then uses a true circular buffer of
// //	only 1024/64=16 points; each element of the circular buffer is that
// //	max of 64 input points. This means that we lose track of where exactly
// //	each input came from, and our output may be off by up to 63 cycles --
// //	but for this application we don't really care.
// ///////////////////////////////////////////////////////////

// struct moving_max {
//     int local_count;	// Position in the 64-element preprocess.
//     float local_max;	// Of the 64-element preprocess.
//     float main_buf[16];	// The 16-element main buffer.
//     int main_buf_idx;	// Circular index into main_buf[].
//     float global_max;	// Biggest in the entire main buffer.
// };
// typedef struct moving_max Moving_max;

// static void moving_max_init (Moving_max *max) {
//     max->local_count = max->local_max = max->main_buf_idx = max->global_max = 0;
//     for (int i=0; i<16; ++i)
// 	max->main_buf[i]=0;
// };

// static float mov_max (Moving_max *max, float data) {
//     // Track the running max of our local 64-sample segment.
//     if (data > max->local_max)  max->local_max = data;
//     // Little trick: also update our approximate global max if needed.
//     if (data > max->global_max) max->global_max = data;

//     // End of a local 64-sample segment.
//     if (++max->local_count == 64) {
// 	// Each element in main_buf[] is a local max of some 64-sample segment.
// 	// Scan through all 16 to find the true max.
// 	max->global_max = -100000;
// 	for (int i=0; i<16; ++i)
// 	    if (max->main_buf[i] > max->global_max)
// 		max->global_max=max->main_buf[i];
// 	// Update the main buffer, replacing its oldest local max with new one
// 	max->main_buf[max->main_buf_idx] = max->local_max;
// 	max->main_buf_idx = (max->main_buf_idx+1) & 0xF;
// 	max->local_count = max->local_max = 0;
//     }
//     return (max->global_max);
// }

// ///////////////////////////////////////////////////////////
// // Moving triangle-template-match filter.
// // This is the clever part of Nguyen2019; it implements
// //	out[N] = (in[N] - in[N-10]) * (in[N] - in[N+10])
// // so that out[N] is highest at a sharp peak. Of course, when data is streaming
// // at us in real time, we don't know in[N+10] until after the fact -- so we
// // cheat a bit and shift everything.
// ///////////////////////////////////////////////////////////

// struct moving_tri {
//     float buf[20];	// The circular buffer
//     int cur_idx;	// Current index into the circular buffer
// };
// typedef struct moving_tri Moving_tri;

// // One-time initialization of Moving_avg struct.
// static void mov_tri_init (Moving_tri *tri) {
//     //...
// }

// // Triangle filter
// static float mov_tri (Moving_tri *tri, float input) {
//     //...
//     return (result);
// }

// ///////////////////////////////////////////////////////////
// // Main code.
// ///////////////////////////////////////////////////////////



// // Set up storage for the three moving averages.
// Moving_avg moving_avg_5Hz,	// 5 Hz on the input.
// 	   moving_avg_35Hz,	// 35Hz on TTM to build lp35.
// 	   moving_avg_thresh_2sec;// last 2 seconds of lp35, for threshold calc
// float buf_5Hz[100], buf_35Hz[14], buf_2sec[1000]; // Circular bufs for the above

// Moving_tri moving_tri;		// Triangle filter building ttm.
// Moving_max moving_thresh_max;	// Maximum of lp35 to compute threshold.

// // Once we detect an R, lock out any more Rs for 125 ticks (250ms), which
// // corresponds to 240 BPM.
// static int lock_count = 0;

// int main() {
//     // Which file are we using?
//     string folder = "./ECG_data";
//     //string file = "3electrode_joel_ecg_6_21_25.csv";
//     //string file = "deena6.csv";
//     string file = "sam_trimmed_3x.csv";
//     analogRead_start(folder + "/" + file, 0);	// Channel 0 or 1.

//     // Initialize our various filters.
//     //mov_avg_init ...
//     mov_tri_init (&moving_tri);
//     moving_max_init (&moving_thresh_max);
    
//     LOG("sample\tnotch60\thp_5Hz\tttm\tlp35\tthresh_2s_avg\tthresh_2s_max\tthresh\tlock_count");

//     for (int i=0; i<6000; ++i) {
// 	int32_t sample = analogRead ();
// 	if (sample == -1) break;

// 	// 60Hz notch filter.
// 	//float notch60 = ...

// 	// 5 Hz highpass, to remove baseline drift and flatten T wave.
// 	// 5Hz = 100 samples @ 2ms/sample. Note that this also turn the input
// 	// into a zero-mean signal (otherwise, the absolute value later
// 	// would be meaningless). 5 Hz = 100 samples.
// 	float avg_5Hz = ...
// 	float hp_5Hz = ...

// 	// Absolute value, in case QRS is inverted
//     float abs = ...

// 	// // Triangle-filter template match. The triangle is 20ms on each side,
// 	// // which is 20 samples total width (10 samples on each side).
// 	// // Keep a 20-sample buffer of 'abs' to help compute this.
// 	float ttm = ...

// 	// // Ttm accentuates the R peak nicely, but also has some nasty
// 	// // oscillations around Q and S. So use a 35Hz low-pass filter.
// 	// // 35Hz is about 29ms; it cuts the height of ttm roughly in half and
// 	// // widens it by about 2x, but smooths most of the oscillations.
// 	float lp35 = ...

// 	// // Threshold computation: get the average & max of lp35 over
// 	// // the last 2 cycles
// 	float thresh = ...

// 	// Add a lockout so we get one-cycle pulses. Lock for .25 sec, or
// 	// 125 cycles
//         if (lock_count>0)	// In a potential R wave
// 	    --lock_count;
// 	...

// 	LOG(sample<<'\t'<< notch60 <<'\t'<< hp_5Hz <<'\t'<< ttm <<'\t'<< lp35
// 	    <<'\t'<< thresh_2s_avg<<'\t'<< thresh_2s_max <<'\t'<<thresh<<'\t'
// 	    <<lock_count);
//     }
//     return (0);
// }