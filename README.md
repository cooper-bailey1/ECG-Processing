# Real-Time ECG Signal Processing on STM32 with FreeRTOS

This project implements a **real-time ECG signal processing and heart rate detection system** on an **STM32L432 (Arduino Nano form factor)** microcontroller using **FreeRTOS**. The system filters raw ECG data, detects R-peaks using a triangle-template matching algorithm, computes heart rate in BPM, drives a buzzer on each beat, and displays BPM on a 7-segment LCD via UART.

The design is based on signal processing techniques discussed in EE152 and operates at a **500 Hz sampling rate** with real-time task scheduling.

---

## Features

- Real-time ECG sampling at **500 Hz**
- **60 Hz notch filter** (biquad IIR)
- **5 Hz high-pass filter** to remove baseline wander
- **Triangle-template peak detection**
- **35 Hz low-pass smoothing**
- **Adaptive thresholding with 2-second statistics**
- **R-peak detection with lockout protection**
- **FreeRTOS multi-task scheduling**
- **Piezo buzzer heartbeat output**
- **Live BPM computation and LCD display**
- **Debug DAC output for signal visualization**

---

## Hardware Setup

| Signal | STM32 Pin | Arduino Nano Pin | Description |
|--------|-----------|------------------|-------------|
| ECG Input | PA0 | A0 | Analog ECG input |
| DAC Output (ECG Debug) | PA4 | A3 | Optional canned ECG output |
| DAC Output (Debug) | PA5 | A4 | Filter debug output |
| LCD UART TX | PA9 | D1 | 7-segment LCD serial interface |
| Buzzer | PA12 | D2 | Piezo buzzer output |
| Debug Pulse | — | D6 | R-peak digital pulse |
| Green LED | PB3 | D13 | System heartbeat LED |


---

## Software Architecture

### FreeRTOS Tasks

| Task | Purpose | Priority |
|------|---------|----------|
| `task_main_loop` | Sampling, filtering, peak detection | `IDLE + 1` |
| `task_blink_grn` | Status LED blink | `IDLE + 2` |
| `task_beep` | Piezo buzzer output | `IDLE` |
| `task_displaybpm` | BPM computation and LCD update | `IDLE` |

---

## Signal Processing Pipeline
ADC → 60Hz Notch → 5Hz High-Pass → |·| →
Triangle Template → 35Hz Low-Pass →
Moving Avg + Moving Max → Adaptive Threshold → R Detection


- **Notch Filter:** Removes power-line interference  
- **High-Pass Filter:** Eliminates baseline drift  
- **Triangle Template Matching:** Accentuates sharp R-peaks  
- **Low-Pass Filter:** Suppresses oscillations  
- **Adaptive Threshold:** Computed using 2-second moving statistics  
- **Lockout:** 250 ms refractory period prevents double-counting  

---

## Heart Rate Detection

- R-peaks generate a **digital pulse on D6**
- Lockout duration: **125 samples = 250 ms**
- Heart rate computed as:
BPM = 60 × 1000 / Δt_ms

- BPM is displayed on a **4-digit 7-segment LCD via UART**
---
