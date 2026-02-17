# Lab Notebook Entry

**Name:**  
**Date:** YYYY-MM-DD  
**Location:** (Lab / Home / ECEB 2070 / etc.)  
**Session Duration:** X hours  

---

## 1. Objectives

Brief, concrete goals for this session.

- Objective 1
- Objective 2
- Objective 3

---

## 2. Work Performed

Describe what was actually done during this session.
Write in past tense. Be factual.

### 2.1 Design / Analysis

Include equations and reasoning used.

Example:

Derived required regulator current:

I_total = I_MCU + I_LCD + I_Sensors  

I_total ≈ 240mA + 60mA + 20mA = 320mA  

Added 30% margin:

I_required = 1.3 × 320mA = 416mA

Selected 1A-rated regulator to ensure thermal headroom.

Reference sources if applicable.

---

### 2.2 Diagrams / Figures

Include numbered figures and reference them in text.

Example:

See Figure 1 for updated block diagram.

![Figure 1: Updated System Block Diagram](../shared/block_diagram_v2.png)

All figures must be labeled and described in the text.

---

### 2.3 Implementation / Build

Document:

- Wiring connections
- Pin assignments
- Component values
- Configuration settings
- Firmware revisions tested

Example:

- GPIO12 → Fan MOSFET gate (100Ω series resistor)
- I2C bus at 400kHz
- Pull-ups: 4.7kΩ

---

## 3. Testing and Results

Document test setup clearly enough to reproduce.

### Test Setup

- Power supply voltage:
- Measurement instrument used:
- Probe location:
- Load conditions:

### Measurements

Measured:

- Vout = 4.98V
- Ripple ≈ 18mVpp
- Current draw ≈ 290mA

### Observations

- Regulator remained below 45°C.
- No instability observed during load transient.

---

## 4. Problems Encountered

Document all non-ideal behavior or confusion.

- Unexpected voltage drop under load
- LCD SPI glitching at 40MHz
- Incorrect sensor reading scaling

Be specific.

---

## 5. Analysis / Debugging

Explain reasoning behind troubleshooting steps.

Example:

Voltage drop likely due to insufficient input capacitance.
Calculated required capacitance using:

ΔV = I / (f × C)

Increased input capacitor from 10µF → 47µF.

---

## 6. Decisions Made

Record decisions and rationale.

- Selected separate analog regulator to reduce noise.
- Disabled WiFi during sampling mode to reduce current spikes.

---

## 7. Next Steps

Clearly define what remains.

- Perform full power budget verification.
- Update schematic to reflect regulator change.
- Validate SPI timing margins.

---
