## Overview
This project implements a real-time ECG (electrocardiogram) acquisition and signal-processing system on an STM32 microcontroller using the STM32 HAL libraries and FreeRTOS. It demonstrates ADC sampling with DMA, real-time filtering, heartbeat detection, and reliable task scheduling suitable for biomedical prototyping and educational purposes.

> **Note:** This project is for educational and prototyping use only. It is NOT a medical device and should not be used for clinical diagnosis or patient care.

## Key Features
- High-sample-rate ECG acquisition using ADC with DMA
- Real-time signal filtering
- QRS detection 
- FreeRTOS-based task architecture for deterministic behavior
- STM32 HAL for hardware abstraction and portability
- Data logging over UART/USB and  live plotting via serial to a host PC
- Safety features: lead-off detection, input protection recommendations, isolation notes

## Hardware Components
- **Microcontroller:** STM32L432KC
- **Right-leg drive** for safety in real ECG acquisition
- **Electrodes & Leads:** standard ECG adhesive electrodes
- **Power:** isolated power supply for patient-connected experiments

## Disclaimer
This project is intended for educational and prototyping purposes only. It is not a medical device and must not be used for diagnostic or clinical decision-making.
"""
