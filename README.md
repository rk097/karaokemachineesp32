# Overview

This project is a karaoke machine running on an ESP32 with real-time microphone input and Bluetooth audio playback. Karaoke is one of my biggest hobbies, so I figured that this would be a fun summer project (summer of ‘25). 

[https://drive.google.com/file/d/1xvGhG7RybF9_WGpnsYIVt7294XtBMqaj/view?usp=sharing](https://drive.google.com/file/d/1xvGhG7RybF9_WGpnsYIVt7294XtBMqaj/view?usp=sharing)

> See it in action!
> 

# Key Docs

![Block Diagram](https://file.notion.so/f/f/dce0f1e2-797a-4225-bacf-896b69545404/8c73ef9f-1fb2-4e75-a91c-147591f6add2/block_diagram.png?table=block&id=24b770ea-64d5-800c-9eb0-cf5cec6d83f0&spaceId=dce0f1e2-797a-4225-bacf-896b69545404&expirationTimestamp=1755748800000&signature=X_-e2vPkIdZAZDg5JqVkYakJabIVtUI4PhHUa_3AB_M&downloadName=block+diagram.png)

[Wiring Diagram](https://file.notion.so/f/f/dce0f1e2-797a-4225-bacf-896b69545404/6f5c912d-381d-4fb7-8032-4daa70507e28/phase1b_revision_(1).pdf?table=block&id=24b770ea-64d5-8063-809a-e8532cb6c82a&spaceId=dce0f1e2-797a-4225-bacf-896b69545404&expirationTimestamp=1755748800000&signature=Kvdq9OLUOCf4rS-XeV-Pk1Zj9XoLNzcCLjPR1AQMpKE&downloadName=phase1b_revision+%281%29.pdf)

[Bill of Materials](https://www.notion.so/Bill-of-Materials-217770ea64d580318d42d4cd06dd8477?pvs=21)

# Project Takeaways

## Summarized Skills

## Embedded Systems Development

- Setting up, fixing dependency issues with, and debugging projects in Platform.io
- Working with dev kits and building projects start-to-finish in ESP-IDF framework
- Using FreeRTOS threads and queues to control audio data access between threads and optimize audio latency
- Maintaining good software engineering principles with easy-to-read code and modular functions
- Smoke testing components with simple code to ensure hardware integrity
- Referencing and carefully **adapting example code** from vendors into my own project environment

## System Design & Project Management

- Creating actionable **roadmaps** for projects in chunks, dividing by core requirements and stretch goal features
- Referencing **datasheets** for part selection and proper software development with respect to product spec
- Making wiring diagrams in **KiCad**, and doing system planning at high level with **block diagrams** and at low level with component selection and pinout mapping
- **Documenting** each step of the development process and maintaining frequent **version control** on **GitHub**

## Audio Engineering

- Understanding both the theory and the actual implementation of **I2S** audio transmission on the **ESP32** and INMP441
- **Signal attenuation** when working with analog devices because of voltage mismatch
- Working with limited resolution **ADC** signals, cleaning the input signal with a **low-pass filter** and noise gate, and converting into I2S
- Using **pointers** to a static **global buffer** for audio input stream for maximum efficiency when working with FreeRTOS queues
- Setting up and working with Bluetooth **A2DP** sinks
- **Mixing** audio input streams of different bit formats (16 bit stereo vs 24 bit mono) into one output stream

## Critical Moments & Design Decisions

### Analog vs. Digital Input Streams

While analog is technically more faithful to the original sound waveform, digital microphones are far easier to work with and produce sound streams at higher resolution and signal quality. I tested both and decided to move forward with the digital mic, the INMP441.

### Global Input Buffer

I use a global variable in `main.c` to hold an array of buffers meant for storing I2S reads over DMA. Though global variables are data unsafe and typically considered bad practice, in this case I decided it was the right thing to do:

1. With a global buffer pool that all tasks can access, we can continually recycle the same buffer pool by generating pointers to different buffers in the pool. It also allows all threads to point to the same buffer, which makes it much easier to control for data races (next point).
2. We prevent data races by keeping track of which threads are using which buffer via FreeRTOS queues. To avoid the overhead of copying entire buffers into FreeRTOS queues, we naturally come to the use of pointers for this purpose. 
3. The simplest and most effective way to use exclusively pointers in FreeRTOS queues while also having the underlying buffer of the pointer stay alive between threads is to use a global variable. `malloc()` runs into heap fragmentation and has far more memory overhead, and stack allocation to temporary buffers made within a thread will not stay alive once the temporary buffer goes out of scope.

### Mixing Audio Input Streams

The INMP441 sends its data in left-justified 24-bit mono I2S (in a 32-bit container), while the A2DP sink sends PCM data in 16-bit stereo I2S. With only two I2S channels available, and one already taken by the INMP441, I had to mix these two into a standard bit format, to output to my one remaining I2S channel. 

Instinctually, one would want to preserve as much precision as possible (as opposed to audio compression), but when trying to upscale 16-bit stereo I2S into 32 bits, no matter what I tried there was always the introduction of noise, whether I played back in mono or stereo. 

Because of this, I revisited my Phase 1B Microphone code and modularly determined that compressing the 24-bit stream into 16-bits and duplicating it from mono to stereo was viable, leading me to ultimately choose the compressed 16-bit stereo output format as my final course of action.
