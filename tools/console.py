"""Console JSON USB: python tools/console.py [porta]. Sem porta, usa a placa detectada.

Porta: COM5 no Windows, /dev/cu.usbserial-XXXX no macOS, /dev/ttyUSB0 no Linux. Digite help.
"""
import argparse
from datetime import datetime
import json
from pathlib import Path
import sys
import threading
import time
import serial
from serial.tools import list_ports
from protocol import LineBuffer, decode_event, encode_command, pgm_image

# USB-serial bridges used by ESP32 boards: WCH CH340 (RoboCore), Silicon Labs CP210x, FTDI.
BOARD_VIDS = {0x1A86, 0x10C4, 0x0403}


def find_board_ports():
    """Serial ports that look like an ESP32 board, on Windows, macOS and Linux."""
    # macOS lists each device as /dev/cu.* and /dev/tty.*; tty.* blocks waiting for carrier detect.
    return sorted(p.device for p in list_ports.comports()
                  if p.vid in BOARD_VIDS and not p.device.startswith("/dev/tty."))


def all_ports():
    """Board ports first, then the others (Bluetooth and similar), without macOS tty.* duplicates."""
    boards = find_board_ports()
    others = sorted(p.device for p in list_ports.comports()
                    if p.device not in boards and not p.device.startswith("/dev/tty."))
    return boards + others


def open_serial(port):
    # Avoid deliberate DTR/RTS reset on opening a running prototype.
    link = serial.Serial(port=None, baudrate=115200, timeout=0.2, write_timeout=2)
    link.dtr = False
    link.rts = False
    link.port = port
    link.open()
    return link


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("port", nargs="?", help="omita para usar a placa detectada")
    parser.add_argument("--logs", type=Path, default=Path("logs"))
    args = parser.parse_args()
    if not args.port:
        found = find_board_ports()
        if len(found) != 1:
            raise SystemExit(f"Informe a porta. Placas detectadas: {found or 'nenhuma'}")
        args.port = found[0]
    args.logs.mkdir(parents=True, exist_ok=True)
    path = args.logs / (datetime.now().strftime("%Y%m%d-%H%M%S") + ".jsonl")
    stop = threading.Event()
    with open_serial(args.port) as link, path.open("ab", buffering=0) as log:
        def receive():
            lines = LineBuffer()
            try:
                while not stop.is_set():
                    for line in lines.feed(link.read(4096)):
                        log.write(line + b"\n")
                        event = decode_event(line)
                        if event and event["event"] == "image":
                            image = path.with_suffix(".pgm")
                            try:
                                image.write_bytes(pgm_image(event))
                                print(f"\nImagem: {image}")
                            except ValueError as exc:
                                print(exc)
                        else:
                            print(line.decode("utf-8", errors="replace"))
            except serial.SerialException as exc:
                print(f"USB desconectada: {exc}", file=sys.stderr)
                stop.set()
        thread = threading.Thread(target=receive, daemon=True)
        thread.start()
        print(f"Log: {path}. help, time, status, snapshot, sample, cancel, test_notify, quit ou JSON.")
        try:
            while not stop.is_set():
                text = input().strip()
                if text == "quit":
                    break
                if text == "help":
                    print('Ex.: {"cmd":"set","profile":1}; {"cmd":"calibrate","label":"empty"}; {"cmd":"arm","delay_s":10}; '
                          '{"cmd":"wifi","ssid":"rede","password":"senha"}; {"cmd":"telegram","token":"123:abc","chat_id":"42"}')
                    continue
                if not text:
                    continue
                try:
                    cmd = json.loads(text) if text.startswith("{") else {"cmd": text}
                    if not isinstance(cmd, dict) or not isinstance(cmd.get("cmd"), str):
                        raise ValueError('Use um objeto JSON com "cmd" textual')
                    if cmd.get("cmd") == "time":
                        cmd.setdefault("epoch", int(time.time()))
                    link.write(encode_command(cmd))
                except (ValueError, serial.SerialException) as exc:
                    print(f"Erro: {exc}")
        except (KeyboardInterrupt, EOFError):
            pass
        finally:
            stop.set()
            thread.join(timeout=2)


if __name__ == "__main__":
    main()
