# ECE 445 Lab Notebook
**Name:** Josh Jenks  
**Project:** Handheld Oscilloscope XY Controller  
**Semester:** Spring 2026  

---

# Entry 1
**Date:** 2026-01-27  
**Session Duration:** ~2 hours  
**Location:** Remote discussion with teammate  

---

## 1. Objectives

- Finalize and submit official project proposal.
- Define high-level system architecture.
- Decompose system into engineering subsystems.
- Establish measurable criteria for success.

---

## 2. Work Performed

### 2.1 Problem Definition

Identified the lack of a simple, purpose-built handheld device capable of generating stable bipolar X/Y signals for oscilloscope XY mode. Existing solutions require multiple bench instruments or ad hoc setups.

Defined goal:

Design a dedicated, handheld “Etch-a-Sketch”-style controller capable of generating stable, low-noise ±5 V analog X/Y outputs with deterministic embedded control.

---

### 2.2 High-Level System Architecture

Defined major signal flow:

User Input (Rotary Encoders + Buttons)  
→ Microcontroller  
→ Dual-Channel DAC  
→ Analog Conditioning (Filter + Level Shift + Buffer)  
→ Bipolar X/Y Outputs (BNC)

Defined power flow:

USB-C 5V Input  
→ 3.3V Digital Rail  
→ Negative Rail Generation  
→ Analog Output Stage

---

### 2.3 Subsystem Decomposition

The system was divided into the following engineering subsystems:

1. **User Input / UI**
   - 2x incremental rotary encoders (quadrature)
   - 4x tactile pushbuttons
   - Optional status LEDs

2. **Microcontroller + Firmware**
   - STM32- or ESP32-class MCU
   - Quadrature decoding
   - Fixed-rate DAC update engine (timer-driven)
   - Drawing modes and position integration logic

3. **Dual-Channel DAC + Analog Output Chain**
   - 12-bit SPI DAC (e.g., MCP4922-class)
   - Voltage reference (~2.5 V)
   - Reconstruction filtering
   - Op-amp gain + level shifting for bipolar output
   - Output buffering and protection
   - 2x PCB-mount BNC connectors

4. **Power Regulation**
   - USB-C 5V sink configuration
   - 3.3V digital regulator
   - Generated negative rail for bipolar output

5. **Stretch Goals**
   - Z-axis blanking output
   - Optional line-level audio output
   - Vector-rendered demo/game mode

---

### 2.4 Engineering Considerations Identified

Discussed key mixed-signal challenges:

- Maintaining low-noise analog outputs while sharing PCB with digital MCU.
- Ensuring deterministic DAC update timing (hardware timer-driven).
- Implementing proper level shifting to achieve ±5 V centered at 0 V.
- Providing output current limiting and clamp protection to prevent scope damage.
- Generating a negative rail from USB 5 V safely and efficiently.

Recognized that careful analog layout and rail isolation would be required.

---

## 3. Criteria for Success Defined

Established measurable success conditions:

- Generate two analog outputs centered at 0 V with selectable range up to ±5 V.
- Demonstrate stable interactive drawing in oscilloscope XY mode.
- Implement timer-driven deterministic DAC update engine.
- Include output protection such that short-to-ground fault does not damage device or scope.

---

## 4. Decisions Made

- Use external dual-channel DAC rather than MCU internal DAC for improved resolution and determinism.
- Use USB-C 5 V as primary power source.
- Include negative rail generation to support true bipolar output.
- Target 12-bit DAC resolution as minimum acceptable resolution.

---

## 5. Next Steps

- Evaluate candidate MCU platforms (STM32 vs ESP32-class).
- Estimate required DAC update rate for smooth drawing.
- Begin preliminary power subsystem planning.
- Draft initial block diagram for internal review.












---

# Entry 2
**Date:** 2026-02-03  
**Session Duration:** ~2–3 hours  
**Location:** Remote discussion + component research  

---

## 1. Objectives

- Select final microcontroller platform.
- Evaluate DAC resolution requirements.
- Confirm deterministic DAC update feasibility.
- Assess SPI throughput requirements.

---

## 2. Work Performed

### 2.1 MCU Platform Evaluation

Compared STM32-class MCUs vs ESP32-class MCUs using the following criteria:

- Available hardware timers
- SPI bandwidth
- USB programming capability
- GPIO availability for encoders/buttons
- Processing headroom for potential stretch goals

Selected **ESP32-S3**.

Rationale:

- Integrated USB programming/debugging.
- Multiple hardware timers suitable for deterministic DAC updates.
- Sufficient processing headroom for real-time UI + waveform generation.
- Strong toolchain and development ecosystem.

---

### 2.2 DAC Resolution Justification

System full-scale output target:

±5 V → 10 Vpp total span.

Evaluated 12-bit DAC resolution:

\[
2^{12} = 4096 \text{ levels}
\]

Voltage per LSB:

\[
\Delta V = \frac{10V}{4096} \approx 2.44 \text{ mV}
\]

For oscilloscope XY visualization, a ~2.4 mV step size is well below visible display resolution and typical noise floor.

Compared to 16-bit resolution:

\[
\Delta V_{16bit} = \frac{10V}{65536} \approx 0.15 \text{ mV}
\]

16-bit precision was determined unnecessary for intended application (vector drawing), and would increase cost and SPI complexity.

Conclusion:

12-bit dual-channel SPI DAC provides sufficient resolution.

---

### 2.3 Deterministic Update Requirement

Recognized that visible intensity variation in XY mode would occur if DAC updates were jittered.

Defined requirement:

DAC updates must be hardware timer driven.

High-level architecture:

Hardware Timer ISR  
→ Push X/Y data to SPI DAC  
→ Maintain constant update interval  

UI processing must not block or delay DAC streaming.

---

### 2.4 SPI Throughput Feasibility

For dual-channel 12-bit DAC:

Data per update:
12 bits × 2 channels = 24 bits (minimum payload)

Assuming conservative 20 kHz update rate:

\[
24 \text{ bits} \times 20000 = 480000 \text{ bits/sec}
\]

Even with protocol overhead, this is << 10 Mbps SPI capability of ESP32-S3.

Conclusion:

SPI bandwidth is not a limiting factor.

---

## 3. Decisions Made

- Selected ESP32-S3 as MCU platform.
- Use external dual-channel 12-bit SPI DAC.
- DAC updates will be hardware-timer driven.
- RF (WiFi/Bluetooth) will be disabled during signal generation to avoid jitter and noise coupling.

---

## 4. Next Steps

- Perform preliminary power budget analysis.
- Refine block diagram.
- Select candidate DAC and op-amp parts.










---

# Entry 3
**Date:** 2026-02-10  
**Session Duration:** ~3 hours  
**Location:** In-person team meeting  

---

## 1. Objectives

- Create formal project proposal.
- Produce refined system block diagram.
- Define high-level system goals.
- Establish subsystem-level requirements.
- Perform preliminary power budget estimation.

---

## 2. Work Performed

### 2.1 Formal Block Diagram

Created refined system block diagram (see Figure 1).

![Figure 1: System Block Diagram – 2026-02-10](/Notebooks/Josh's/Block_diagram.png)

Subsystems included:

- Power subsystem (USB-C, 3.3V LDO, −5V charge pump)
- MCU subsystem (ESP32-S3)
- UI subsystem (2 encoders + 4 buttons)
- Dual 12-bit DAC
- Analog conditioning (op-amps)
- BNC X/Y outputs

Signal paths defined:

- SPI (MCU → DAC)
- Quadrature A/B (Encoders → MCU)
- GPIO (Buttons → MCU)
- Analog outputs → BNC connectors

---

### 2.2 High-Level System Goals

Defined three primary goals:

1. Functional Output  
2. Real-Time User Interaction  
3. Single-Cable USB-C Operation  

These serve as measurable top-level project objectives.

---

### 2.3 Subsystem Requirements

**Power Subsystem**
- Generate stable +3.3 V, +5 V, −5 V rails.
- Provide sufficient current for MCU and analog stages.
- Avoid brownout or instability under peak load.

**MCU Subsystem**
- Support USB programming.
- Stream dual-channel DAC data deterministically.
- Process UI inputs without interfering with timing.

**Analog Subsystem**
- Produce ±5 V outputs centered at 0 V.
- Maintain signal integrity over intended bandwidth.
- Include output current limiting and protection.

**UI Subsystem**
- Reliable quadrature decoding.
- Button debouncing.
- No interference with DAC streaming.

---

### 2.4 Preliminary Power Budget Estimation

Estimated peak current draw:

ESP32-S3 (peak, RF disabled) ≈ 200–240 mA  
Dual DAC ≈ 5–10 mA  
Op-amp stages ≈ 10–20 mA  
UI components ≈ negligible  

Estimated total peak:

\[
I_{est} \approx 260–300 \text{ mA}
\]

Added 30% design margin:

\[
I_{required} \approx 1.3 \times 300mA \approx 390mA
\]

Conclusion:

3.3V regulator should be rated ≥ 500 mA to ensure margin.

Also identified that −5V rail must support op-amp load current with sufficient charge pump capacity.

---

### 2.5 Architectural Clarifications

Confirmed:

- Separate digital (3.3V) and analog (±5V) domains.
- Level shifting required to center output at 0 V.
- Output stage must tolerate short-to-ground condition.
- Careful PCB layout required for mixed-signal integrity.

---

## 3. Decisions Made

- Formalized subsystem boundaries.
- Locked system architecture.
- Confirmed preliminary regulator current targets.
- Approved block diagram for proposal submission.

---

## 4. Next Steps

- Begin schematic capture.
- Select specific DAC and op-amp part numbers.
- Refine negative rail generation approach.
- Quantify maximum intended output frequency.
