"""Console JSON USB; python tools/console.py COM5. Type help for shortcuts."""
import argparse
from datetime import datetime
import json
from pathlib import Path
import sys
import threading
import time
import serial
from protocol import LineBuffer, decode_event, encode_command, pgm_image


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
    parser.add_argument("port")
    parser.add_argument("--logs", type=Path, default=Path("logs"))
    args = parser.parse_args()
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
