#!/usr/bin/env python3
"""
Send the firmware-side "enter download mode" trigger over USB-CDC.

The firmware (main.cpp `onUsbCdcRx`) watches for the bytes "STOPWATCH-DL\n"
and on match writes RTC_CNTL_OPTION1_REG and esp_restart()s into the ROM
download bootloader. After this script returns, `pio run -t upload` can
sync directly without a manual BOOT-button long-press.

Best-effort: silently exits 0 if no `/dev/cu.usbmodem*` port is present, so
the Makefile can invoke this unconditionally before `pio upload`. If the
running firmware predates the handler, the bytes are just logged and the
flash falls back to whatever esptool can do (typically: needs manual BOOT).

No external deps — uses os.open / os.write so pyserial isn't required.
"""
from __future__ import annotations
import glob, os, sys, time

MAGIC = b"STOPWATCH-DL\n"
WAIT_AFTER_S = 1.5   # let the chip reboot + USB re-enumerate

def main() -> int:
    ports = sorted(glob.glob("/dev/cu.usbmodem*"))
    if not ports:
        return 0
    port = ports[0]
    try:
        fd = os.open(port, os.O_WRONLY | os.O_NOCTTY | os.O_NONBLOCK)
    except OSError as e:
        print(f"zap: open {port}: {e}", file=sys.stderr)
        return 0
    try:
        os.write(fd, MAGIC)
    except OSError as e:
        print(f"zap: write {port}: {e}", file=sys.stderr)
    finally:
        os.close(fd)
    print(f"zap: sent flash trigger to {port}; waiting {WAIT_AFTER_S}s for download mode")
    time.sleep(WAIT_AFTER_S)
    return 0

if __name__ == "__main__":
    raise SystemExit(main())
