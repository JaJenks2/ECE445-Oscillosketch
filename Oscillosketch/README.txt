OscilloSketch live-audio v5 continuous-playback refactor notes

This patch keeps the v4 direct DAC playback path because that is what made the audio sound clean. The main changes target the remaining start/stop behavior shown in the v4 logs.

What was wrong in v4:
- The Python sender used locally estimated fill by adding frames when it sent packets, even though many packets were failing CRC on the ESP.
- The ESP inserted silence for sequence gaps. That made fill look healthy even when the buffer contained mostly manufactured silence rather than real accepted audio.
- The sender could burst packets too aggressively during prefill/refill, which likely overloaded the USB CDC / Arduino Serial receive path and caused CRC errors, sequence jumps, and fake fill.

What v5 changes:
- Python now treats ESP STAT fill as the source of truth. It no longer increases receiver fill just because a packet was written locally.
- Python prefill/refill is rate-limited. It still catches up faster than realtime, but it no longer blasts tens of packets back-to-back.
- Python flushes each packet write so the OS serial queue does not accumulate large bursts.
- ESP sequence-gap handling no longer inserts silence into the ring. Missing packets are counted, but only real accepted PCM contributes to buffer fill.
- ESP audio RX task priority is raised and its local read chunk is increased.

Expected good behavior:
- fill should represent real accepted audio, not fake inserted silence.
- acc should rise close to sent over time. It does not need to match perfectly, but if acc is far below sent, the link is still dropping/corrupting packets.
- crc and seqerr should rise much more slowly than in v4.
- over should stay 0.
- und should stay 0 during normal playback.

Recommended test:
1. Upload this firmware.
2. Put the board in Audio/USB_STREAM mode.
3. Run:
   py stream_audio_serial.py --file "<path-to-audio>"
4. For first test, use the 500 Hz sine file. Then test the MP3.
5. Send back 15-30 seconds of HOST summaries if playback still pauses.

Interpretation:
- If fill slowly falls to 2048 and playback pauses, accepted throughput is still below 16 kHz.
- If crc rises quickly, the host is still sending too aggressively or the CDC path is dropping bytes.
- If seqerr/miss rises while crc is low, whole packets are being skipped.
- If over rises, STREAM_HIGH_FILL_FRAMES is too high or refill pacing is too aggressive.
