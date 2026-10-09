"""Compila e grava o firmware no Windows, macOS ou Linux.

    python tools/build.py                 # compila
    python tools/build.py upload          # grava na placa detectada (ou --port)
    python tools/build.py clean

Use o Python do .venv, onde o PlatformIO está instalado. No Windows, um alias temporário de
unidade (subst) encurta o caminho do projeto: o ESP-IDF falha com caminhos longos (MAX_PATH).
"""
import argparse
import os
from pathlib import Path
import string
import subprocess
import sys

ROOT = Path(__file__).resolve().parents[1]


def platformio(args, cwd, env):
    return subprocess.run([sys.executable, "-m", "platformio", *args], cwd=cwd, env=env).returncode


def windows_short_path(env, args):
    # PlatformIO's relative-path handling needs a non-root project folder, so map the parent.
    used = {f"{d}:" for d in string.ascii_uppercase if os.path.exists(f"{d}:\\")}
    drive = next((d for d in ("V:", "W:", "X:", "Y:", "Z:") if d not in used), None)
    if not drive:
        raise SystemExit("Nenhuma letra livre entre V: e Z: para abreviar o caminho.")
    if subprocess.run(["subst", drive, str(ROOT.parent)]).returncode:
        raise SystemExit("Nao foi possivel criar o alias de caminho com subst.")
    short = Path(f"{drive}\\") / ROOT.name
    try:
        env["GIT_CEILING_DIRECTORIES"] = os.pathsep.join(
            filter(None, [str(ROOT), str(short), env.get("GIT_CEILING_DIRECTORIES")]))
        return platformio(args, short, env)
    finally:
        subprocess.run(["subst", drive, "/d"])


def main():
    parser = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    parser.add_argument("action", nargs="?", default="build", choices=["build", "upload", "clean"])
    parser.add_argument("--port", help="COM5, /dev/cu.usbserial-XXXX ou /dev/ttyUSB0; omita para detectar")
    args = parser.parse_args()
    pio = ["run", "-e", "esp32cam"]
    if args.action == "upload":
        port = args.port
        if not port:
            sys.path.insert(0, str(ROOT / "tools"))
            from console import find_board_ports
            found = find_board_ports()
            if len(found) != 1:
                raise SystemExit(f"Informe --port. Placas detectadas: {found or 'nenhuma'}")
            port = found[0]
            print(f"Placa detectada em {port}")
        pio += ["-t", "upload", "--upload-port", port]
    elif args.action == "clean":
        pio += ["-t", "clean"]
    env = os.environ.copy()
    # Keep the SDK's version probe from walking into this application's Git repository.
    env["GIT_CEILING_DIRECTORIES"] = os.pathsep.join(filter(None, [str(ROOT), env.get("GIT_CEILING_DIRECTORIES")]))
    code = windows_short_path(env, pio) if os.name == "nt" else platformio(pio, ROOT, env)
    sys.exit(code)


if __name__ == "__main__":
    main()
