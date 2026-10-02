"""USB JSON Lines helpers shared by the panel and command line.

No network service, server or patient data is required.
"""
import base64
import json


def encode_command(command):
    data = json.dumps(command, ensure_ascii=True, allow_nan=False, separators=(",", ":")).encode()
    if len(data) > 511:
        raise ValueError("Comando excede 511 bytes")
    return data + b"\n"


SECRETS = ("password", "token")


def redact(command):
    """Copy of a command safe to display or log: Wi-Fi password and bot token hidden."""
    return {k: ("***" if k in SECRETS and v else v) for k, v in command.items()}


def decode_event(line):
    try:
        event = json.loads(line)
    except (ValueError, UnicodeDecodeError):
        return None  # ROM/ESP-IDF diagnostic lines are retained in the raw log.
    return event if isinstance(event, dict) and isinstance(event.get("event"), str) else None


def pgm_image(event):
    if event.get("width") != 160 or event.get("height") != 120:
        raise ValueError("Dimensoes inesperadas")
    data = base64.b64decode(event["gray_base64"], validate=True)
    if len(data) != 160 * 120:
        raise ValueError("Imagem incompleta")
    return b"P5\n160 120\n255\n" + data


class LineBuffer:
    """Accumulate partial UART reads; a serial timeout is not a line boundary."""
    def __init__(self):
        self.buffer = bytearray()
        self.discarding = False

    def feed(self, data):
        lines = []
        for byte in data:
            if byte == 10:
                if not self.discarding and self.buffer:
                    lines.append(bytes(self.buffer))
                self.buffer.clear()
                self.discarding = False
            elif not self.discarding:
                self.buffer.append(byte)
                if len(self.buffer) > 65536:
                    self.buffer.clear()
                    self.discarding = True
        return lines
