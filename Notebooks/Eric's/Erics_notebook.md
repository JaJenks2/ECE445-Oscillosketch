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

By the end of the session, we decided to use the ESP32-S3FN8 as our microcontroller and an external dual-channel 12-bit SPI DAC for generating the X/Y signals. [ESP32-S3 datasheet](https://www.espressif.com/sites/default/files/documentation/esp32-s3_datasheet_en.pdf)

---

# Entry 3
**Date:** 02-10-2026
**Session Duration:** ~3 hours  
**Location:** ECEB

---

For this meeting, we needed to create the formal project proposal and produce a refined block diagram, along with clear high-level goals and subsystem requirements. We also wanted an early power estimate so we could pick realistic regulators and make sure the USB-C supply would be enough.

![Figure 1: System Block Diagram – 02-10-2026](/Notebooks/Eric's/Block_diagram.png)

We made a cleaner system block diagram (Figure 1) that shows the full design from end to end: USB-C power → 3.3 V LDO + −5 V charge pump, an ESP32-S3, two encoders + four buttons, a dual 12-bit DAC, op-amp conditioning, and BNC X/Y outputs. We also clearly labeled the main connections: SPI from the MCU to the DAC, encoder A/B into the MCU, buttons into GPIO, and analog outputs to the BNC connectors.

We then set three top-level project goals that the whole design must meet: (1) functional ±5 V X/Y output, (2) real-time user control (smooth drawing without lag), and (3) single-cable USB-C operation. After that, we broke the system into subsystems (power, MCU/firmware, analog output, UI) and wrote down what each one must do so we can test it later.

We also did a quick power budget to make sure our parts make sense. With RF disabled, we estimated the ESP32-S3 could draw about 200–240 mA, the DAC about 5–10 mA, and the op-amps about 10–20 mA, giving roughly 260–300 mA peak total. With extra margin, we concluded the 3.3 V regulator should be rated at least 500 mA, and we noted the −5 V rail needs enough charge pump capacity to handle the op-amp load.

By the end, we confirmed the overall architecture (separate digital 3.3 V and analog ±5 V domains, level shifting to center at 0 V, short-to-ground tolerant output stage, and careful mixed-signal PCB layout). We approved the updated block diagram and used it as the basis for the proposal. Next, we plan to start the schematic, choose exact DAC and op-amp parts, refine the negative rail approach, and decide the maximum output frequency we want to support.

---

# Entry 4
**Date:** 02-12-2026
**Session Duration:** ~3 hours  
**Location:** ACES Funk Library

---

For this meeting, we planned on picking all of the components and starting the schematic. 

Here is the list of components that ended up getting chosen:
- Inverting Charge Pump: LM27762DSST [link]  (https://www.digikey.com/en/products/detail/texas-instruments/LM27762DSST/6234957)
- Microcontroller: ESP32-S3FN8 [link] (https://www.digikey.com/en/products/detail/espressif-systems/ESP32-S3FN8/15822446)
- Dual 12-bit DAC: MCP4922 [link] (https://www.digikey.com/en/products/detail/microchip-technology/MCP4922-E-SL/716258)
- Dual Op-Amp: OPA2192 [link] (https://www.digikey.com/en/products/detail/texas-instruments/OPA2192QDGKRQ1/8322744)
- LDO: AZ1117C [link] (https://www.digikey.com/en/products/detail/diodes-incorporated/AZ1117CH-3-3TRG1/4470985)
- TVS Diode Protection: TPD2EUSB30ADRTR [link] (https://www.digikey.com/en/products/detail/texas-instruments/TPD2EUSB30ADRTR/2520830)
- USB_Connector: USB4215 [link] (https://www.digikey.com/en/products/detail/gct/USB4215-03-A/24395489)
- Buffer Op-Amp: TLV9061IDBVR [link] (https://www.digikey.com/en/products/detail/texas-instruments/TLV9061IDBVR/9771994?s=N4IgTCBcDaICoBkBqBOADANgIwEkAiAQkiALoC%2BQA)
- BNC Connector: CONBNC002 [link] (https://www.digikey.com/en/products/detail/texas-instruments/TLV9061IDBVR/9771994?s=N4IgTCBcDaICoBkBqBOADANgIwEkAiAQkiALoC%2BQA)
- Rotary Encoder: PEC11R-4220F-S0024 [link] (https://www.digikey.com/en/products/detail/bourns-inc/PEC11R-4220F-S0024/4499660?gclsrc=aw.ds&gad_source=1&gad_campaignid=20504615262&gbraid=0AAAAADrbLlh-Xpv7BeVk2DolS7mw4Ei7L&gclid=CjwKCAiAncvMBhBEEiwA9GU_fng9zQn0728rQmIBpjGYQPMPQt_0od9oJhHU58XtyYJZ6IpkS2r6sRoCvZ4QAvD_BwE)

For the KICAD schematic, we made sure to put lots of test points for debugging in the future. At the moment, we are unsure how to power the ESP32-S3 since there are many different examples online. Shown below is the current unfinished schematic (Figure 2). 

![Figure 2: Unfinished Schematic – 02-12-2026](/Notebooks/Eric's/Unfinished_Schematic.png)
