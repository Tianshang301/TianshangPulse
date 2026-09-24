#!/usr/bin/env python3
"""Capture N seconds of serial output from the S3, with an optional DTR reset
first so we catch boot logs. Prints the raw decoded stream to stdout.

Usage: capture_serial.py [PORT] [SECONDS] [--reset] [--raw]

--raw writes the undecoded byte stream to scripts/serial_dump.bin instead of
decoding to stdout. Use when a panic backtrace is being truncated in UTF-8.
"""
import os
import sys
import time

import serial

# PowerShell's default stdout codepage is GBK on zh-CN Windows and chokes on
# binary bytes from the ROM boot ROM. Force UTF-8 with replace so we never
# crash mid-capture.
try:
    sys.stdout.reconfigure(encoding="utf-8", errors="replace")
except Exception:
    pass


def main() -> int:
    argv = [a for a in sys.argv[1:] if not a.startswith("--")]
    flags = [a for a in sys.argv[1:] if a.startswith("--")]
    port = argv[0] if len(argv) > 0 else "COM3"
    seconds = int(argv[1]) if len(argv) > 1 else 14
    reset = "--reset" in flags

    try:
        ser = serial.Serial(port, 115200, timeout=1)
    except Exception as exc:
        print("OPEN_FAIL %s: %s" % (port, exc))
        return 1

    ser.reset_input_buffer()
    if reset:
        # Drive DTR/RTS: CH340 pulls EN low on the RTS edge -> cold reset.
        ser.dtr = True
        ser.rts = True
        time.sleep(0.2)
        ser.dtr = False
        ser.rts = False
        time.sleep(1.5)

    start = time.time()
    buf = bytearray()
    out = sys.stdout
    while time.time() - start < seconds:
        try:
            data = ser.read(4096)
        except Exception as exc:
            print("\nREAD_ERR %s" % exc)
            break
        if data:
            buf.extend(data)
            if "--raw" not in flags:
                out.write(data.decode("utf-8", "replace"))
                out.flush()

    ser.close()
    elapsed = time.time() - start

    if "--raw" in flags:
        dump = os.path.join(os.path.dirname(os.path.abspath(__file__)), "serial_dump.bin")
        with open(dump, "wb") as fh:
            fh.write(bytes(buf))
        print("RAW DUMP -> %s (%d bytes in %.1fs)" % (dump, len(buf), elapsed))
        return 0

    print("\n=== EOF CAPTURE (%d bytes in %.1fs) ===" % (len(buf), elapsed))
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
