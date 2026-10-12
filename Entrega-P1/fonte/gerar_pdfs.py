"""Gera Relatorio_Tecnico_P1.pdf e Apresentacao_P1.pdf a partir dos HTML desta pasta.

Uso: python Entrega-P1/fonte/gerar_pdfs.py   (Windows, macOS ou Linux; usa Edge ou Chrome sem janela)
Coloque as fotos em Entrega-P1/fonte/fotos/ (prototipo.jpg, placa.jpg, painel.png, telegram.png)
e rode de novo: os quadros tracejados são trocados pelas imagens. Os PDFs saem em Entrega-P1/.
"""
import shutil
import subprocess
import sys
import tempfile
from pathlib import Path

HERE = Path(__file__).resolve().parent
OUT = HERE.parent
JOBS = [("relatorio.html", "Relatorio_Tecnico_P1.pdf"), ("apresentacao.html", "Apresentacao_P1.pdf")]
CANDIDATES = [
    r"C:\Program Files (x86)\Microsoft\Edge\Application\msedge.exe",
    r"C:\Program Files\Microsoft\Edge\Application\msedge.exe",
    r"C:\Program Files\Google\Chrome\Application\chrome.exe",
    "/Applications/Google Chrome.app/Contents/MacOS/Google Chrome",
    "/Applications/Microsoft Edge.app/Contents/MacOS/Microsoft Edge",
    "/Applications/Chromium.app/Contents/MacOS/Chromium",
]


def browser():
    for c in CANDIDATES:
        if Path(c).exists():
            return c
    for name in ("google-chrome", "chromium", "chromium-browser", "microsoft-edge", "msedge", "chrome"):
        found = shutil.which(name)
        if found:
            return found
    sys.exit("Edge/Chrome não encontrado: instale o Google Chrome ou ajuste CANDIDATES.")


def main():
    exe = browser()
    for src, out in JOBS:
        target = OUT / out
        with tempfile.TemporaryDirectory() as profile:
            subprocess.run([exe, "--headless", "--disable-gpu", "--no-pdf-header-footer",
                            "--allow-file-access-from-files", f"--user-data-dir={profile}",
                            "--virtual-time-budget=8000", f"--print-to-pdf={target}",
                            (HERE / src).as_uri()], check=True,
                           stdout=subprocess.DEVNULL, stderr=subprocess.DEVNULL)
        print(f"{out}: {target.stat().st_size // 1024} KB")


if __name__ == "__main__":
    main()
