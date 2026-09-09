# Conversational Hand Gestures Demo

*An inclusive, offline hand-gesture "communication bridge" for children, running two AI workloads on a single microcontroller under µT-Kernel 3.0*

🔗 [Introduction page](https://www.gnomons.com/conversational-hand-gestures-demo/)

## Overview
- Conversational Hand Gestures is an interactive demo built for children and anyone who communicates better without voice or touch: a child raises an open palm, a cartoon face on screen greets them, and the two hold a short conversation, a game, a story, and a goodbye — all through five hand shapes.
- The TRON × AI theme here is combining an RTOS and AI rather than running one alongside the other. On µT-Kernel 3.0, two independent AI workloads — computer vision and language generation — share a single Renesas RA8P1 chip through the kernel's own scheduling and memory primitives, so a slow language model can never stall a live camera pipeline.
- No network stack is used, and no camera data ever leaves the device, so the conversation stays private to the child using it.

## Development Environment and Software
- Development is performed on a Windows PC using Renesas e² studio as the integrated development environment.
  - The target board is the Renesas EK-RA8P1 evaluation kit, with a MIPI camera expansion board and a parallel graphics display expansion board.
  - Toolchain: Renesas e² studio, FSP (Flexible Software Package), and LLVM for Arm (ATfE).
  - Serial terminal: TeraTerm.
- The software is written in C and uses µT-Kernel 3.0 APIs to implement real-time tasks for camera capture, AI inference, UI/conversation state, and story generation.

## Feature Overview
Conversational Hand Gestures recognizes five hand shapes and drives an interaction loop entirely through gestures, with no keyboard or touch input. The following functions are implemented by four application tasks running on µT-Kernel, coordinated through the kernel's fixed task priorities.

- **Gesture recognition (vision pipeline)**
An MIPI camera feeds a palm-detection model and a hand-landmark model, both running on the on-chip Ethos-U55 NPU. The resulting 21 joint positions become one of five named gestures (open palm, fist, victory, thumbs up, thumbs down) through geometry ratio tests on the Cortex-M85 — no embedding model, no network call.

- **Story generation (language pipeline)**
A "Tiny" Llama2 model (`stories15M`, 8-bit quantized) writes a short story one token at a time. Weights are read directly out of external Octo-SPI flash rather than copied into RAM, and its int8 matrix multiplication calls the Cortex-M85's Helium vector instructions directly — sixteen int8 lanes per instruction instead of one.

- **Fixed-priority scheduling**
Four application tasks share the chip under µT-Kernel's fixed priorities: camera capture (highest), AI inference (high), UI/conversation state machine (medium), and story generation (lowest). Story generation only consumes CPU time left over after the other three, so the camera picture and story text keep updating together with no dropped frames.

- **Signaling and shared memory**
The four tasks coordinate through a single µT-Kernel event flag carrying only ready/done bits — an interrupt sets bits directly, with nothing in between that could overflow. All other data (processed images, joint positions, gesture results, story text) travels in structures each written by exactly one task, so no two tasks race over the same memory.

- **Conversational interaction loop**
A child can greet the device, play a round of rock-paper-scissors against it, or have it tell a short AI-generated story, with the animated face reacting to mimic real conversational turn-taking rather than a single command triggering a single response.

> 📘 **New to this demo? Start with the [User Manual](user-manual.md).** It walks through board setup, switch/jumper settings, building and downloading the project, and how to run and troubleshoot the demo.
