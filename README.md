# OscilloSketch

**OscilloSketch** is a handheld embedded system that turns a standard oscilloscope operating in XY mode into an interactive vector display.

Developed for **ECE 445: Senior Design at the University of Illinois Urbana-Champaign**, OscilloSketch uses an ESP32-S3, a dual-channel 12-bit DAC, a precision analog output stage, rotary encoders, and custom firmware to generate synchronized X/Y signals for drawing, vector graphics, gameplay, and audio visualization.

The completed system is powered entirely over USB-C, produces approximately **±5 V X/Y outputs**, and updates coordinated X/Y points at approximately **110 kHz**.

**Team 22 — Spring 2026**  
Josh Jenks · Eric Vo

OscilloSketch was one of 10 projects, from roughly 100 teams, to receive an ECE 445 Honorable Mention and induction into the **ECE 445 Hall of Fame**, placing it approximately in the top 10% of projects that semester.

## Features

OscilloSketch supports four primary operating modes:

- **Etch-a-Sketch** — Two rotary encoders control X and Y cursor position. The generated path is stored and continuously replayed so the complete drawing remains visible on the oscilloscope.
- **Shape Demo** — Predefined vector patterns demonstrate coordinated X/Y waveform generation.
- **Pong** — A real-time vector implementation of Pong includes paddle control, ball physics, collision detection, scoring, and vector-rendered digits.
- **Audio Visualization** — A precomputed stereo PCM audio clip is played from ESP32 flash while its left and right channels drive the X and Y outputs. The encoders provide real-time low-pass and high-pass filter control.

The firmware also supports **Z-axis blanking** on compatible oscilloscopes to suppress visible retrace lines between disconnected vector primitives.

## System Architecture

```text
Rotary Encoders / Buttons
           │
           ▼
      ESP32-S3
           │
       20 MHz SPI
           │
           ▼
     MCP4922 DAC
       RAW X / Y
        0–3.3 V
           │
           ▼
 Precision Analog Stage
           │
      ~−5 V to +5 V
        ┌──┴──┐
        ▼     ▼
      BNC X  BNC Y
        │     │
        └──┬──┘
           ▼
      Oscilloscope
         XY Mode
```

The hardware operates from a single **5 V USB-C input**. An AZ1117C generates the 3.3 V digital rail, while an LM27762 charge pump supplies the bipolar analog rails used by the output stage. An MCP4922 DAC generates the raw X/Y signals, and an OPA2192-based analog stage scales and level-shifts them to approximately ±5 V.

The firmware separates application logic from the timing-critical output path. The main application loop handles input, mode selection, drawing updates, and game logic, while a dedicated FreeRTOS task continuously replays synchronized X/Y points through the DAC. This final dual-core architecture achieved a stable update rate of approximately **110 kHz**, exceeding the original 10 kHz requirement by roughly 11×.

## Performance Summary

| Parameter | Measured result |
|---|---:|
| X/Y resolution | 12 bit |
| Coordinated update rate | **110 kHz** |
| SPI clock | **20 MHz** |
| USB input current | **~98 mA** |
| Input-to-output latency | **~19.2 ms** |
| Final X/Y output range | **~−4.88 V to +4.99 V** |
| Analog step size | **2.41 mV/code** |
| X/Y update mismatch | **~560 ns** |

The completed system met all four primary project requirements: 12-bit X/Y resolution with an approximately ±5 V range, operation from a single USB-C input below 500 mA, input latency no greater than 20 ms, and at least 10,000 coordinated X/Y updates per second.

## Building and Running

The firmware targets the **ESP32-S3-WROOM-1-N16** using the Arduino ESP32 environment.

1. Install the ESP32 Arduino board package.
2. Select the ESP32-S3 target and configure it for **16 MB flash**.
3. Select an application partition large enough for the embedded PCM audio; **Huge APP** may be required.
4. Open the final `Oscillosketch.ino` sketch, compile it, and upload it over USB-C.
5. Connect the X and Y BNC outputs to an oscilloscope and enable XY display mode.
6. If supported by the oscilloscope, connect the Z output to use beam blanking.

Audio used by the final demonstration was converted offline to 16-bit PCM and stored in ESP32 flash. The included `make_embedded_song.py` utility generates the embedded audio header and requires FFmpeg and `pydub`.

```bash
python make_embedded_song.py "/path/to/song.mp3" --seconds 30 --rate 16000 --channels 2
```

## Repository Contents

- [Firmware](./firmware/) — Final ESP32-S3 source code and audio-conversion utility
- [Lab notebooks](./lab-notebooks/) — Chronological development, testing, and debugging records
- [Final report](./final-report/) — Complete system design, calculations, schematics, verification procedures, and measured results

The final report is the authoritative source for detailed hardware design, requirements, verification results, and engineering analysis. The lab notebooks preserve the project’s development history, including PCB bring-up, firmware architecture revisions, timing measurements, audio experiments, and final integration.

## Authors

**Josh Jenks**  
**Eric Vo**

ECE 445 — Senior Design  
University of Illinois Urbana-Champaign  
Spring 2026
