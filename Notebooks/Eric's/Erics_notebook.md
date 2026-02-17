# ECE 445 Lab Notebook

**Name:**  Eric Vo
**Project:** OscilloSketch
**Semester:** Spring 2026

---

# Entry 1
**Date:** 01-27-2026  
**Session Duration:** ~2 hours  
**Location:** Remote

---

We finalized and submitted the Request for Approval, where we proposed to create a handheld device that generates stable, low-noise bipolar ±5V X/Y signals for scilloscope XY mode. This helps users create custom waveforms without messing around with complex bench instruments. The project would entail building a small device that turns user input into clean X and Y voltages. Two rotary encoders would let the user “move” the dot left/right and up/down, and buttons would handle things like reset and mode changes. A microcontroller would read the encoders and update the outputs at a steady, fixed rate using a hardware timer.

Signal Flow:
1. MCU reads user input through rotary encoders and buttons
2. MCU processes data and outputs SPI into dual-channel DAC
3. DAC outputs analog XY and gets filtered, offset, and scaled to range from -5v to +5v
4. Output scaled analog output to BNC, which connects to the oscilloscope

Power Flow:
1. Take USB-C 5v input
2. Feed 5v into the 3.3v LDO to power the MCU and DAC
3. Feed 5v into the inverting charge pump for -5v to power the op-amp

Subsystem:
- User Input
- Microcontroller + Firmware
- Daul-Channel DAC + Analog Output
- Power Regulation

Required for Success:
- Generate two analog outputs centered at 0 V with selectable range up to ±5 V
- Demonstrate stable interactive drawing in oscilloscope XY mode
- Implement timer-driven deterministic DAC update engine
- Include output protection such that a short-to-ground fault does not damage the device or the scope

We have thought of some cool stretch goals, like having z-axis blanking output, which allows us to disconnect the continuous line and vector-rendered demo/game mode, which allows us to display prewrite images onto the oscilloscope. 

However, some of the concerns we have are adding an SD card or USB drive, since that adds another unknown layer to this project. We worry that there won't be an easy way to upload the data for an image or game and then read from the SD card itself. Another thing we have to think more about is which MCU to use, since I am more comfortable with the STM32, but it is very complex and intricate. Josh has mentioned the ESP32, but I have no experience with it and don't know if I can get it to flash, which is a big issue. We also do not know which resolution to get for the DAC, since the more bits, the finer the drawing will be, but at a certian points it is not noticeable.

By the end of our discussion, we submitted the RFA and decided on how the power and signal would flow through the project. For the future, we need to make sure we know exactly what components to use. 

---

# Entry 2
**Date:** 02-03-2026
**Session Duration:** ~2-3 hours  
**Location:** Remote

---

During this meeting, we wanted to discuss the components and what the requirements for the components should be, along with their cost. The main components that we will discuss today are the MCU, DAC, and the inverting charge pump. 

We compared the STM32 MCU against the ESP32 MCU based on timers, SPI speed, USB programming, and how many GPIO pins we would need. Overall, they both had the same functionality and speed for timers. By the end, we chose the ESP32-S3 because it has built-in USB programming, plenty of timers for steady updates, enough speed for UI + drawing features, and a strong software ecosystem.

We also checked whether a 12-bit DAC is good enough for drawing. Since our output range is -5 V to +5 V (10 V total span), a 12-bit DAC gives 4096 steps, which works out to about 10 V / 4096 ≈ 2.4 mV per step. For oscilloscope XY drawings, that step size is smaller than what you can easily see on-screen and is usually smaller than the noise you’ll already have, so going to 16-bit wasn’t worth the extra cost and extra complexity.

Another big point was making sure the drawing looks consistent on the scope. If the DAC update timing jumps around, the brightness can look uneven, and the picture can flicker. So we set a firm rule that the DAC updates must be driven by a hardware timer, meaning the update loop runs on a fixed schedule. The UI code (reading encoders/buttons, mode changes, etc.) should never be allowed to delay the DAC update stream.

Finally, we sanity-checked SPI speed. Even at a conservative 20 kHz update rate, sending two 12-bit values is only 24 bits per update, which is roughly 24 × 20,000 = 480,000 bits/sec (about 0.48 Mbps) before overhead. That’s far below what the ESP32-S3 SPI can handle, so SPI speed won’t be our bottleneck as long as the DAC can also handle that fast of SPI speed. To reduce noise, power consumption, and timing issues, we also decided to turn off WiFi/Bluetooth during signal generation. 

By the end of the session, we decided to use the ESP32-S3FN8 as our microcontroller and an external dual-channel 12-bit SPI DAC for generating the X/Y signals.


