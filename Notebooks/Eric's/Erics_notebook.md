# ECE 445 Lab Notebook

**Name:**  Eric Vo
**Project:** OscilloSketch
**Semester:** Spring 2026

---

# Entry 1
**Date:** 2026-01-27  
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

Required for Success:
- Generate two analog outputs centered at 0 V with selectable range up to ±5 V
- Demonstrate stable interactive drawing in oscilloscope XY mode
- Implement timer-driven deterministic DAC update engine
- Include output protection such that a short-to-ground fault does not damage the device or the scope

We have thought of some cool stretch goals, like having z-axis blanking output, which allows us to disconnect the continuous line and vector-rendered demo/game mode, which allows us to display prewrite images onto the oscilloscope. 

However, some of the concerns we have are adding an SD card or USB drive, since that adds another unknown layer to this project. We worry that there won't be an easy way to upload the data for an image or game and then read from the SD card itself. Another thing we have to think more about is which MCU to use, since I am more comfortable with the STM32, but it is very complex and intricate. Josh has mentioned the ESP32, but I have no experience with it and don't know if I can get it to flash, which is a big issue. We also do not know which resolution to get for the DAC, since the more bits, the finer the drawing will be, but at a certian points it is not noticeable.

By the end of our discussion, we submitted the RFA and decided on using a 12-bit DAC with the ESP32. The ESP32 is more flexible and is easier to code. The 12-bit DAC should be more than enough, since it allows for 4096 different voltage points for both X and Y. 


