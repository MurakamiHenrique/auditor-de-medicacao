import base64
from pathlib import Path
import sys
import unittest
sys.path.insert(0, str(Path(__file__).resolve().parents[1] / "tools"))
from protocol import LineBuffer, decode_event, encode_command, pgm_image, redact
from evaluate import evaluate


class ProtocolTests(unittest.TestCase):
    def test_serial_fragments_and_rom_noise(self):
        stream = LineBuffer()
        self.assertEqual(stream.feed(b'{"event":"sta'), [])
        self.assertEqual(decode_event(stream.feed(b'tus"}\n')[0]), {"event": "status"})
        self.assertIsNone(decode_event(b"ESP-ROM boot"))
        self.assertIsNone(decode_event(b"[]"))
        self.assertIsNone(decode_event(b'\xff'))

    def test_oversize_discard_and_recovery(self):
        stream = LineBuffer()
        self.assertEqual(stream.feed(b"x" * 70000 + b"\n"), [])
        self.assertEqual(stream.feed(b"ok\n"), [b"ok"])
        with self.assertRaises(ValueError):
            encode_command({"cmd": "x" * 512})
        with self.assertRaises(ValueError):
            encode_command({"x": float("nan")})

    def test_redact_secrets(self):
        command = {"cmd": "wifi", "ssid": "casa", "password": "segredo123"}
        self.assertEqual(redact(command), {"cmd": "wifi", "ssid": "casa", "password": "***"})
        self.assertEqual(command["password"], "segredo123")  # original still sent to the board
        self.assertEqual(redact({"cmd": "telegram", "token": "1:x", "chat_id": "42"})["token"], "***")

    def test_snapshot(self):
        event = {"width": 160, "height": 120, "gray_base64": base64.b64encode(b"\x80" * 19200).decode()}
        self.assertTrue(pgm_image(event).startswith(b"P5\n160 120\n255\n"))
        event["gray_base64"] = "AA=="
        with self.assertRaises(ValueError):
            pgm_image(event)

    def test_metrics_reject_insufficient_evidence(self):
        events = [{"event": "observation", "ms": 10, "label": "empty", "duration_ms": 2500}]
        report = evaluate(events, [{"ms": "10", "truth": "unknown"}])
        self.assertEqual(report["unknown_classified_empty"], 1)
        self.assertFalse(report["capture_target_met"])
        self.assertFalse(report["classification_target_met"])

    def test_metrics_reject_duplicate_labels_and_boots(self):
        events = [{"event": "observation", "ms": 10, "label": "present", "duration_ms": 100}]
        label = {"ms": "10", "truth": "present"}
        with self.assertRaises(ValueError):
            evaluate(events, [label, label])
        with self.assertRaises(ValueError):
            evaluate([{"event": "boot"}, {"event": "boot"}], [])


if __name__ == "__main__":
    unittest.main()
