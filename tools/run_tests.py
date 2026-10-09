"""Compile and execute the SAME portable C++ core used by the firmware.

Use --cxx g++, --cxx clang++, or install ziglang==0.13.0 in the venv.
"""
import argparse
import importlib.util
import os
from pathlib import Path
import shutil
import subprocess
import sys

ROOT = Path(__file__).resolve().parents[1]


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--cxx")
    args = parser.parse_args()
    output = ROOT / "artifacts" / "tests"
    output.mkdir(parents=True, exist_ok=True)
    compiler = args.cxx or shutil.which("g++") or shutil.which("clang++")
    if compiler:
        command = [compiler]
    else:
        zig = importlib.util.find_spec("ziglang")
        if not zig:
            raise SystemExit("Instale um compilador C++ (macOS: xcode-select --install; Linux: g++) "
                             "ou: python -m pip install ziglang==0.13.0")
        executable = Path(zig.origin).parent / ("zig.exe" if os.name == "nt" else "zig")
        command = [str(executable), "c++"]
    env = os.environ.copy()
    env["ZIG_GLOBAL_CACHE_DIR"] = str(output / "zig-cache")
    binary = output / ("core_tests.exe" if os.name == "nt" else "core_tests")
    subprocess.run(command + ["-std=c++17", "-Wall", "-Wextra", "-Werror", "-O1", "-g",
                   "-I", str(ROOT / "components/auditor/include"),
                   str(ROOT / "components/auditor/core.cpp"), str(ROOT / "tests/core_tests.cpp"),
                   "-o", str(binary)], check=True, cwd=ROOT, env=env)
    subprocess.run([str(binary)], check=True, cwd=ROOT)
    subprocess.run([sys.executable, "-m", "unittest", "discover", "-s", "tests", "-v"], check=True, cwd=ROOT)


if __name__ == "__main__":
    main()
