import argparse
import math
import struct
import time
import sys
import ctypes
import zlib
from pathlib import Path

import serial
from serial import SerialTimeoutException
from serial.tools import list_ports
from pydub import AudioSegment

MAGIC0 = 0xA5
MAGIC1 = 0x5A
PKT_TYPE_START = 0x10
PKT_TYPE_DATA_S16 = 0x11
PKT_TYPE_STOP = 0x12
FLAGS = 0x00

AUDIO_SAMPLE_RATE = 16000
AUDIO_PACKET_FRAMES = 256
AUDIO_SERIAL_BAUD = 2_000_000

# ESP firmware thresholds should match config.h.
PREFILL_TARGET_FRAMES = 8192
STREAM_TARGET_FILL_FRAMES = 10000
STREAM_HIGH_FILL_FRAMES = 14000
STREAM_LOW_FILL_FRAMES = 4096

START_ACK_TIMEOUT_S = 2.0
STOP_ACK_TIMEOUT_S = 1.0
SUMMARY_PERIOD_S = 1.0


class WindowsHighResTimer:
    def __init__(self, period_ms=1):
        self.period_ms = period_ms
        self.enabled = False

    def __enter__(self):
        if sys.platform.startswith("win"):
            try:
                winmm = ctypes.WinDLL("winmm")
                res = winmm.timeBeginPeriod(self.period_ms)
                if res == 0:
                    self.enabled = True
            except Exception:
                self.enabled = False
        return self

    def __exit__(self, exc_type, exc, tb):
        if self.enabled and sys.platform.startswith("win"):
            try:
                winmm = ctypes.WinDLL("winmm")
                winmm.timeEndPeriod(self.period_ms)
            except Exception:
                pass


def choose_serial_port(explicit_port):
    if explicit_port:
        return explicit_port

    ports = list(list_ports.comports())
    if not ports:
        raise RuntimeError("No serial ports found.")

    print("Available serial ports:")
    for idx, p in enumerate(ports):
        desc = f"{p.device}"
        if p.description:
            desc += f" - {p.description}"
        print(f"  [{idx}] {desc}")

    while True:
        sel = input("Select serial port index: ").strip()
        try:
            idx = int(sel)
            if 0 <= idx < len(ports):
                return ports[idx].device
        except ValueError:
            pass
        print("Invalid selection.")


def choose_audio_file(explicit_path):
    if explicit_path:
        p = Path(explicit_path).expanduser().resolve()
        if not p.exists():
            raise FileNotFoundError(f"Audio file not found: {p}")
        return p

    try:
        import tkinter as tk
        from tkinter import filedialog

        root = tk.Tk()
        root.withdraw()
        filename = filedialog.askopenfilename(
            title="Select audio file",
            filetypes=[
                ("Audio files", "*.mp3 *.wav *.flac *.ogg *.m4a *.aac"),
                ("All files", "*.*"),
            ],
        )
        root.destroy()

        if not filename:
            raise RuntimeError("No file selected.")
        return Path(filename).expanduser().resolve()

    except Exception as e:
        raise RuntimeError(
            "No file path provided and file picker failed. Pass the file path with --file."
        ) from e


def load_and_convert_audio(path):
    seg = AudioSegment.from_file(path)
    seg = seg.set_frame_rate(AUDIO_SAMPLE_RATE)
    seg = seg.set_channels(2)
    seg = seg.set_sample_width(2)

    raw = seg.raw_data
    if len(raw) % 4 != 0:
        raw += b"\x00" * (4 - (len(raw) % 4))
    return raw


def iter_packets(raw_pcm):
    bytes_per_frame = 4
    total_frames = len(raw_pcm) // bytes_per_frame
    idx = 0

    while idx < total_frames:
        frame_count = min(AUDIO_PACKET_FRAMES, total_frames - idx)
        start = idx * bytes_per_frame
        end = start + frame_count * bytes_per_frame
        payload = raw_pcm[start:end]
        yield frame_count, payload
        idx += frame_count


def crc_for_packet_fields(pkt_type, seq, frame_count, payload):
    crc_fields = struct.pack("<BBHH", pkt_type, FLAGS, seq & 0xFFFF, frame_count)
    return zlib.crc32(crc_fields + payload) & 0xFFFFFFFF


def build_packet(pkt_type, seq, frame_count=0, payload=b""):
    crc = crc_for_packet_fields(pkt_type, seq, frame_count, payload)
    header = struct.pack(
        "<BBBBHHI",
        MAGIC0,
        MAGIC1,
        pkt_type,
        FLAGS,
        seq & 0xFFFF,
        frame_count,
        crc,
    )
    return header + payload


def build_start_payload():
    return struct.pack("<IHBB", AUDIO_SAMPLE_RATE, AUDIO_PACKET_FRAMES, 2, 2)


def open_serial_for_esp32_s3_usb(port):
    ser = serial.Serial()
    ser.port = port
    ser.baudrate = AUDIO_SERIAL_BAUD
    ser.timeout = 0
    ser.write_timeout = 2
    ser.dsrdtr = False
    ser.rtscts = False

    ser.open()

    try:
        ser.setDTR(True)
        ser.setRTS(False)
    except Exception:
        pass

    return ser


def write_full_packet(ser, pkt):
    try:
        written = ser.write(pkt)
    except SerialTimeoutException:
        raise RuntimeError("Serial write timed out while sending a packet.")

    if written != len(pkt):
        raise RuntimeError(f"Short write while sending packet: {written} of {len(pkt)} bytes")


def precise_wait_until(target_time, ser=None, streamer=None):
    while True:
        now = time.perf_counter()
        remaining = target_time - now
        if remaining <= 0:
            return

        if ser is not None and streamer is not None:
            streamer.drain_status(ser)

        if remaining > 0.003:
            time.sleep(remaining - 0.001)
        elif remaining > 0.0005:
            time.sleep(remaining - 0.0002)
        else:
            pass


def parse_key_value_line(line):
    parts = line.strip().split()
    if not parts:
        return None

    kind = parts[0]
    if kind not in {"ACK", "STAT", "ERR"}:
        return None

    out = {"line_type": kind}
    if kind == "ACK":
        if len(parts) < 2:
            return None
        out["ack_kind"] = parts[1]
        kv_parts = parts[2:]
    else:
        kv_parts = parts[1:]

    for token in kv_parts:
        if "=" not in token:
            continue
        k, v = token.split("=", 1)
        try:
            out[k] = int(v, 0)
        except ValueError:
            out[k] = v

    return out


class StreamController:
    def __init__(self):
        self.rx_text_buf = ""
        self.last_status = None
        self.last_ack = None
        self.est_fill = 0.0
        self.est_time = time.perf_counter()
        self.total_packets_written = 0
        self.total_frames_written = 0
        self.write_errors = 0

    def decay_estimate(self):
        now = time.perf_counter()
        elapsed = now - self.est_time
        if elapsed > 0:
            self.est_fill = max(0.0, self.est_fill - elapsed * AUDIO_SAMPLE_RATE)
            self.est_time = now

    def note_sent_frames(self, frames):
        self.decay_estimate()
        self.est_fill += frames
        self.total_frames_written += frames

    def note_status(self, stat):
        if "fill" in stat:
            self.est_fill = float(stat["fill"])
            self.est_time = time.perf_counter()
        self.last_status = stat

    def drain_status(self, ser, print_unknown=False):
        try:
            waiting = ser.in_waiting
        except Exception:
            waiting = 0

        if waiting <= 0:
            self.decay_estimate()
            return []

        data = ser.read(waiting)
        parsed = []
        if data:
            self.rx_text_buf += data.decode("utf-8", errors="ignore")

            while "\n" in self.rx_text_buf:
                line, self.rx_text_buf = self.rx_text_buf.split("\n", 1)
                line = line.strip()
                if not line:
                    continue

                msg = parse_key_value_line(line)
                if msg is None:
                    if print_unknown:
                        print(f"[ESP] {line}")
                    continue

                parsed.append(msg)
                if msg["line_type"] == "STAT":
                    self.note_status(msg)
                elif msg["line_type"] == "ACK":
                    self.last_ack = msg
                    if "fill" in msg:
                        self.est_fill = float(msg["fill"])
                        self.est_time = time.perf_counter()
                elif msg["line_type"] == "ERR":
                    print(f"[ESP] {line}")

        self.decay_estimate()
        return parsed


def wait_for_start_ack(ser, ctrl, start_seq):
    pkt = build_packet(PKT_TYPE_START, start_seq, 0, build_start_payload())
    deadline = time.perf_counter() + START_ACK_TIMEOUT_S
    next_send = 0.0

    while time.perf_counter() < deadline:
        now = time.perf_counter()
        if now >= next_send:
            write_full_packet(ser, pkt)
            next_send = now + 0.100

        msgs = ctrl.drain_status(ser)
        for msg in msgs:
            if (
                msg["line_type"] == "ACK"
                and msg.get("ack_kind") == "START"
                and msg.get("seq") == start_seq
            ):
                return msg

        time.sleep(0.001)

    raise RuntimeError("No START ACK from ESP. Confirm the board is powered and the correct COM port is selected.")


def wait_for_stop_ack(ser, ctrl, stop_seq):
    pkt = build_packet(PKT_TYPE_STOP, stop_seq, 0, b"")
    deadline = time.perf_counter() + STOP_ACK_TIMEOUT_S
    next_send = 0.0

    while time.perf_counter() < deadline:
        now = time.perf_counter()
        if now >= next_send:
            write_full_packet(ser, pkt)
            next_send = now + 0.100

        msgs = ctrl.drain_status(ser)
        for msg in msgs:
            if (
                msg["line_type"] == "ACK"
                and msg.get("ack_kind") == "STOP"
                and msg.get("seq") == stop_seq
            ):
                return msg

        time.sleep(0.001)

    return None


def send_data_packet(ser, ctrl, seq, frame_count, payload):
    pkt = build_packet(PKT_TYPE_DATA_S16, seq, frame_count, payload)
    write_full_packet(ser, pkt)
    ctrl.total_packets_written += 1
    ctrl.note_sent_frames(frame_count)


def print_summary(ctrl, packets_sent, done=False):
    ctrl.decay_estimate()
    stat = ctrl.last_status or {}
    prefix = "[HOST DONE]" if done else "[HOST]"
    print(
        f"{prefix} sent={packets_sent} est_fill={ctrl.est_fill:.0f} "
        f"fill={stat.get('fill', '?')} max={stat.get('max', '?')} "
        f"acc={stat.get('acc', '?')} miss={stat.get('miss', '?')} "
        f"crc={stat.get('crc', '?')} seqerr={stat.get('seqerr', '?')} "
        f"over={stat.get('over', '?')} und={stat.get('und', '?')}"
    )


def stream_loop(port, raw_pcm, loop_file=False):
    packet_list = list(iter_packets(raw_pcm))
    if not packet_list:
        raise RuntimeError("Decoded audio contained no frames.")

    with WindowsHighResTimer(1):
        with open_serial_for_esp32_s3_usb(port) as ser:
            print(f"Opened {port} @ {AUDIO_SERIAL_BAUD} baud")
            print("DTR asserted, RTS low.")
            print("High-resolution timer requested.")
            time.sleep(0.25)

            ser.reset_input_buffer()
            ser.reset_output_buffer()

            ctrl = StreamController()

            start_seq = 0
            ack = wait_for_start_ack(ser, ctrl, start_seq)
            print(
                f"Transport START acked, fill={ack.get('fill')} "
                f"firmware_rate={ack.get('rate')} firmware_pkt={ack.get('pkt')}"
            )

            seq = 0
            packet_index = 0
            loop_count = 0
            packets_sent = 0
            last_summary_time = time.perf_counter()
            next_realtime_send = time.perf_counter()

            print(f"Prefilling to about {PREFILL_TARGET_FRAMES} frames...")
            while ctrl.est_fill < PREFILL_TARGET_FRAMES and packet_index < len(packet_list):
                ctrl.drain_status(ser)
                frame_count, payload = packet_list[packet_index]
                send_data_packet(ser, ctrl, seq, frame_count, payload)
                packets_sent += 1
                seq = (seq + 1) & 0xFFFF
                packet_index += 1

            # Give the ESP a short chance to report the true receiver-side fill.
            prefill_deadline = time.perf_counter() + 0.250
            while time.perf_counter() < prefill_deadline:
                ctrl.drain_status(ser)
                if ctrl.last_status and ctrl.last_status.get("fill", 0) >= PREFILL_TARGET_FRAMES:
                    break
                time.sleep(0.001)

            print_summary(ctrl, packets_sent)
            print("Streaming. Press Ctrl+C to stop.")

            try:
                while True:
                    ctrl.drain_status(ser)

                    if packet_index >= len(packet_list):
                        if loop_file:
                            packet_index = 0
                            loop_count += 1
                            print(f"Looped file {loop_count} time(s)")
                        else:
                            break

                    ctrl.decay_estimate()
                    now = time.perf_counter()

                    if ctrl.est_fill >= STREAM_HIGH_FILL_FRAMES:
                        time.sleep(0.002)
                        continue

                    # If the receiver is below target, send immediately to refill.
                    # If it is near target, fall back to real-time pacing.
                    if ctrl.est_fill >= STREAM_TARGET_FILL_FRAMES:
                        precise_wait_until(next_realtime_send, ser, ctrl)
                    else:
                        next_realtime_send = time.perf_counter()

                    frame_count, payload = packet_list[packet_index]
                    send_data_packet(ser, ctrl, seq, frame_count, payload)
                    packets_sent += 1
                    seq = (seq + 1) & 0xFFFF
                    packet_index += 1

                    packet_duration = frame_count / AUDIO_SAMPLE_RATE
                    next_realtime_send += packet_duration
                    if next_realtime_send < time.perf_counter():
                        next_realtime_send = time.perf_counter()

                    now = time.perf_counter()
                    if now - last_summary_time >= SUMMARY_PERIOD_S:
                        print_summary(ctrl, packets_sent)
                        last_summary_time = now

            except KeyboardInterrupt:
                print("\nStopping transport...")
            else:
                print("\nAll packets sent; waiting for ESP playback buffer to drain...")

                drain_deadline = time.perf_counter() + max(2.0, (ctrl.est_fill / AUDIO_SAMPLE_RATE) + 1.0)
                while time.perf_counter() < drain_deadline:
                    ctrl.drain_status(ser)
                    ctrl.decay_estimate()
                    stat_fill = None
                    if ctrl.last_status is not None:
                        stat_fill = ctrl.last_status.get("fill")
                    effective_fill = stat_fill if stat_fill is not None else ctrl.est_fill
                    if effective_fill <= AUDIO_PACKET_FRAMES:
                        break
                    time.sleep(0.005)

            stop_seq = seq
            ack = wait_for_stop_ack(ser, ctrl, stop_seq)
            if ack is not None:
                print(f"Transport STOP acked, fill={ack.get('fill')}")
            else:
                print("STOP ACK not received; stream may already be closed.")

            ctrl.drain_status(ser)
            print_summary(ctrl, packets_sent, done=True)
            print("Stopped.")


def main():
    parser = argparse.ArgumentParser(description="Live-audio stream to OscilloSketch.")
    parser.add_argument("--port", help="Serial port, e.g. COM13")
    parser.add_argument("--file", help="Audio file path")
    parser.add_argument("--loop", action="store_true", help="Loop the selected file until Ctrl+C")
    args = parser.parse_args()

    audio_path = choose_audio_file(args.file)
    port = choose_serial_port(args.port)

    print(f"Loading: {audio_path}")
    raw_pcm = load_and_convert_audio(audio_path)

    total_frames = len(raw_pcm) // 4
    duration_s = total_frames / AUDIO_SAMPLE_RATE
    print(f"Converted to {AUDIO_SAMPLE_RATE} Hz stereo 16-bit PCM")
    print(f"Frames: {total_frames}")
    print(f"Duration: {duration_s:.2f} s")
    print(f"Packets: {math.ceil(total_frames / AUDIO_PACKET_FRAMES)}")

    stream_loop(port, raw_pcm, loop_file=args.loop)


if __name__ == "__main__":
    main()
