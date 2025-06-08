ESP32 Frequency-to-PWM with ESP-NOW Transmission
This project implements a frequency detection and PWM generation system using an ESP32. It reads an input signal's frequency using the ESP32's Pulse Counter (PCNT) hardware, compares it with a base reference frequency, maps the frequency difference to a PWM duty cycle, and transmits the result wirelessly via ESP-NOW to a peer ESP32 device.

🧠 Key Features
Frequency Measurement: Uses hardware PCNT on pin GPIO 1 to measure input frequency (e.g., from a beat signal or oscillator).

PWM Output: Maps frequency difference to an 8-bit PWM signal on GPIO 2.

Wireless Communication: Transmits PWM values via ESP-NOW to another ESP32 (define peer MAC).

Base Frequency Calibration:

Manual: Press a button (GPIO 19) to set the base reference frequency.

Auto: When the signal is stable, the base frequency updates automatically.

Stability Detection: Uses smoothed frequency changes to determine signal stability.

📦 Hardware Requirements
ESP32 (e.g., ESP32-S3)

Signal input (e.g., sensor output or waveform generator)

Push button (connected to GPIO 19)

PWM-controllable device (e.g., LED or servo)

Another ESP32 as receiver (for ESP-NOW)

📶 Pin Configuration
Function	GPIO	Description
Frequency Input	1	Input signal for PCNT
Button Input	19	Manual base frequency set
PWM Output	2	Outputs PWM (0–255)

📋 License
MIT License
