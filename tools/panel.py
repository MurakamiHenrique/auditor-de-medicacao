"""Painel local USB para configurar o auditor. Agenda, inferencia e avisos rodam no ESP32."""
from datetime import datetime
import json
from pathlib import Path
import queue
import threading
import time
import tkinter as tk
from tkinter import ttk, messagebox, filedialog
import serial
from console import all_ports, find_board_ports, open_serial
from protocol import LineBuffer, decode_event, encode_command, pgm_image, redact

STATES = {"idle": "CONFIGURAÇÃO — agenda parada", "armed": "AGENDADO — aguardando o horário",
          "monitoring": "VERIFICANDO A DOSE", "removed": "RETIRADA CONFIRMADA",
          "not_loaded": "COMPARTIMENTO VAZIO NO HORÁRIO", "alert": "ALERTA — dose ainda presente",
          "late_removed": "RETIRADA COM ATRASO", "fault": "FALHA — retirada não confirmada"}


class Panel:
    def __init__(self, root):
        self.root = root
        self.link = None
        self.events = queue.Queue()
        self.stop = threading.Event()
        self.reader = None
        self.log = None
        self.image_data = None
        self.seq = 0
        self.last_contact = 0
        self.draw_start = None
        self.roi = [40, 24, 80, 72]
        self.pending = {}
        self.loaded_settings = False
        root.title("Auditor visual de medicação • configuração USB")
        root.geometry("1000x800")
        top = ttk.Frame(root, padding=10); top.pack(fill="x")
        self.port = ttk.Combobox(top, width=28)
        self.port.pack(side="left")
        self.ports()
        ttk.Button(top, text="Atualizar portas", command=self.ports).pack(side="left")
        ttk.Button(top, text="Conectar", command=self.connect).pack(side="left")
        ttk.Button(top, text="Desconectar", command=self.disconnect).pack(side="left")
        ttk.Button(top, text="Estado", command=lambda: self.send("status")).pack(side="left")
        self.state = tk.StringVar(value="USB desconectada")
        ttk.Label(root, textvariable=self.state, font=("Arial", 16, "bold"), wraplength=950).pack(pady=6)
        ttk.Label(root, text="Um compartimento • uma verificação por dia • avisos pelo Telegram • "
                             "retirada visual não comprova ingestão").pack()
        body = ttk.Frame(root, padding=10); body.pack(fill="x")
        visual = ttk.Frame(body); visual.pack(side="left", anchor="n")
        self.canvas = tk.Canvas(visual, width=480, height=360, background="#203040", highlightthickness=0)
        self.canvas.pack()
        self.canvas.bind("<ButtonPress-1>", self.begin_roi)
        self.canvas.bind("<B1-Motion>", self.drag_roi)
        self.canvas.bind("<ButtonRelease-1>", self.end_roi)
        ttk.Label(visual, text="Imagem sob demanda: arraste para recortar o compartimento.").pack()
        ttk.Button(visual, text="Capturar imagem USB", command=lambda: self.send("snapshot")).pack(side="left")
        ttk.Button(visual, text="Salvar PGM", command=self.save_image).pack(side="left")
        tabs = ttk.Notebook(body); tabs.pack(side="left", fill="both", expand=True, padx=(16, 0))
        self.camera_tab(tabs)
        self.schedule_tab(tabs)
        self.network_tab(tabs)
        self.info = tk.StringVar(value="Configure Wi-Fi, Telegram, pinagem, ROI e as duas referências. "
                                       "Depois disso o aparelho funciona sem o computador.")
        ttk.Label(root, textvariable=self.info, wraplength=950).pack(fill="x", padx=10)
        self.output = tk.Text(root, height=10, state="disabled", wrap="word")
        self.output.pack(fill="both", expand=True, padx=10, pady=10)
        root.protocol("WM_DELETE_WINDOW", self.close)
        root.after(100, self.poll)

    def camera_tab(self, tabs):
        tab = ttk.Frame(tabs, padding=8); tabs.add(tab, text="Câmera")
        self.profile = ttk.Combobox(tab, state="readonly", width=33,
                                   values=["0 — pinagem não confirmada", "1 — AI Thinker / ESP32-CAM", "2 — WROVER KIT (placa RoboCore do grupo)"])
        self.profile.current(0); self.profile.pack(fill="x")
        ttk.Button(tab, text="Aplicar perfil de pinagem", command=lambda: self.send("set", profile=self.profile.current())).pack(fill="x")
        self.roi_text = tk.StringVar(value="ROI: 40, 24, 80, 72")
        ttk.Label(tab, textvariable=self.roi_text).pack(pady=(8, 0))
        ttk.Button(tab, text="Salvar ROI (apaga referências)", command=lambda: self.send("set", roi=self.roi)).pack(fill="x")
        ttk.Button(tab, text="1. Calibrar VAZIO", command=lambda: self.send("calibrate", label="empty")).pack(fill="x", pady=(8, 3))
        ttk.Button(tab, text="2. Calibrar PRESENTE", command=lambda: self.send("calibrate", label="present")).pack(fill="x")
        ttk.Button(tab, text="Testar leitura (envia ao Telegram)",
                   command=lambda: self.send("sample", notify=True)).pack(fill="x", pady=3)
        ttk.Button(tab, text="Limpar calibração", command=lambda: self.send("clear_calibration")).pack(fill="x")

    def schedule_tab(self, tabs):
        tab = ttk.Frame(tabs, padding=8); tabs.add(tab, text="Agenda")
        self.fields = {}
        for key, label, default in [("hour", "Hora (0–23)", "8"), ("minute", "Minuto", "0"),
                                     ("window_s", "Tolerância (segundos)", "60"),
                                     ("interval_s", "Intervalo entre leituras (s)", "5"),
                                     ("late_s", "Observar após o alerta (s)", "1800")]:
            row = ttk.Frame(tab); row.pack(fill="x", pady=1)
            ttk.Label(row, text=label).pack(side="left")
            var = tk.StringVar(value=default); self.fields[key] = var
            ttk.Entry(row, textvariable=var, width=8).pack(side="right")
        self.enabled = tk.BooleanVar(value=True)
        ttk.Checkbutton(tab, text="Verificação diária ativa", variable=self.enabled).pack(anchor="w", pady=3)
        ttk.Button(tab, text="Salvar agenda", command=self.schedule).pack(fill="x", pady=3)
        ttk.Button(tab, text="Teste: verificar em 10 s", command=lambda: self.send("arm", delay_s=10)).pack(fill="x")
        ttk.Button(tab, text="Cancelar verificação em andamento", command=lambda: self.send("cancel")).pack(fill="x", pady=3)
        ttk.Label(tab, text="Sem Wi-Fi, use o botão abaixo para ajustar a hora pela USB.", wraplength=300).pack(pady=(8, 0))
        ttk.Button(tab, text="Ajustar hora manualmente", command=lambda: self.send("time", epoch=int(time.time()))).pack(fill="x")

    def network_tab(self, tabs):
        tab = ttk.Frame(tabs, padding=8); tabs.add(tab, text="Wi-Fi e Telegram")
        self.net = {}
        for key, label, secret in [("ssid", "Rede Wi-Fi (SSID)", False), ("password", "Senha do Wi-Fi", True),
                                    ("token", "Token do bot (BotFather)", True), ("chat_id", "chat_id do cuidador", False)]:
            ttk.Label(tab, text=label).pack(anchor="w")
            var = tk.StringVar(); self.net[key] = var
            ttk.Entry(tab, textvariable=var, show="•" if secret else "").pack(fill="x")
            if key == "password":
                ttk.Button(tab, text="Salvar Wi-Fi", command=self.save_wifi).pack(fill="x", pady=(3, 10))
        ttk.Button(tab, text="Salvar Telegram", command=self.save_telegram).pack(fill="x", pady=3)
        ttk.Button(tab, text="Enviar mensagem de teste", command=lambda: self.send("test_notify")).pack(fill="x")
        ttk.Label(tab, text="As credenciais ficam só no ESP32 e não aparecem no log.", wraplength=300).pack(pady=(8, 0))

    def ports(self):
        # Board first (CH340 etc.): COMx on Windows, /dev/cu.usbserial-* on macOS.
        self.port["values"] = all_ports()
        boards = find_board_ports()
        if boards and self.port.get() not in boards:
            self.port.set(boards[0])

    def write(self, text):
        self.output.configure(state="normal")
        self.output.insert("end", text + "\n")
        if int(self.output.index("end-1c").split(".")[0]) > 250:
            self.output.delete("1.0", "50.0")
        self.output.see("end"); self.output.configure(state="disabled")

    def connect(self):
        self.disconnect()
        try:
            self.link = open_serial(self.port.get())
            Path("logs").mkdir(exist_ok=True)
            path = Path("logs") / (datetime.now().strftime("%Y%m%d-%H%M%S") + ".jsonl")
            self.log = path.open("ab", buffering=0)
            self.info.set(f"Registros: {path.resolve()}")
            self.stop = threading.Event()
            self.loaded_settings = False
            self.last_contact = time.monotonic()
            self.reader = threading.Thread(target=self.receive, args=(self.link, self.log, self.stop), daemon=True)
            self.reader.start()
            self.send("status")
        except (OSError, serial.SerialException) as exc:
            self.disconnect(); messagebox.showerror("USB", str(exc))

    def receive(self, link, log, stop):
        lines = LineBuffer()
        try:
            while not stop.is_set():
                for line in lines.feed(link.read(4096)):
                    log.write(line + b"\n")
                    self.events.put((decode_event(line), line.decode("utf-8", errors="replace")))
        except (OSError, serial.SerialException) as exc:
            self.events.put(({"event": "disconnected"}, str(exc)))

    def disconnect(self):
        self.stop.set()
        if self.reader:
            self.reader.join(timeout=1)
        if self.link:
            self.link.close()
        if self.log:
            self.log.close()
        self.reader = self.link = self.log = None
        self.pending.clear()
        while not self.events.empty():
            self.events.get_nowait()
        self.state.set("USB desconectada")

    def send(self, cmd, **kwargs):
        if not self.link:
            messagebox.showerror("USB", "Conecte a placa primeiro."); return
        try:
            self.seq += 1
            payload = {"cmd": cmd, "id": self.seq, **kwargs}
            self.link.write(encode_command(payload))
            self.pending[self.seq] = (cmd, time.monotonic())
            self.write("> " + json.dumps(redact(payload), ensure_ascii=False))
        except (ValueError, serial.SerialException) as exc:
            messagebox.showerror("Comando", str(exc))

    def schedule(self):
        try:
            values = {k: int(v.get()) for k, v in self.fields.items()}
        except ValueError:
            messagebox.showerror("Agenda", "Use números inteiros."); return
        self.send("set", enabled=self.enabled.get(), **values)

    def save_wifi(self):
        self.send("wifi", ssid=self.net["ssid"].get(), password=self.net["password"].get())
        self.net["password"].set("")

    def save_telegram(self):
        self.send("telegram", token=self.net["token"].get().strip(), chat_id=self.net["chat_id"].get().strip())
        self.net["token"].set("")

    def begin_roi(self, event):
        self.draw_start = (max(0, min(159, event.x // 3)), max(0, min(119, event.y // 3)))

    def drag_roi(self, event):
        if self.draw_start is None:
            return
        x, y = self.draw_start
        ex, ey = max(0, min(160, event.x // 3)), max(0, min(120, event.y // 3))
        self.roi = [min(x, ex), min(y, ey), abs(ex - x), abs(ey - y)]
        self.draw_roi()

    def end_roi(self, event):
        self.drag_roi(event)
        self.draw_start = None

    def draw_roi(self):
        x, y, w, h = self.roi
        self.canvas.delete("roi")
        self.canvas.create_rectangle(x * 3, y * 3, (x + w) * 3, (y + h) * 3, outline="#ffce45", width=2, tags="roi")
        self.roi_text.set(f"ROI: {x}, {y}, {w}, {h}")

    def save_image(self):
        if self.image_data:
            filename = filedialog.asksaveasfilename(defaultextension=".pgm", filetypes=[("PGM", "*.pgm")])
            if filename:
                Path(filename).write_bytes(self.image_data)

    def show_status(self, event):
        s = event["settings"]
        state = STATES.get(event["state"], event["state"])
        if event["state"] == "armed" and event.get("next_epoch"):
            state += " " + datetime.fromtimestamp(event["next_epoch"]).strftime("(%d/%m %H:%M)")
        if event.get("demo"):
            state += " • TESTE"
        self.state.set(state)
        self.profile.current(s["profile"])
        if not self.loaded_settings:
            # Do not overwrite fields/ROI while the operator is editing them.
            for key, var in self.fields.items():
                var.set(str(s[key]))
            self.enabled.set(s["enabled"])
            self.roi = list(s["roi"])
            self.draw_roi()
            self.loaded_settings = True
        last = event.get("last_result", "idle")
        when = datetime.fromtimestamp(event["last_result_epoch"]).strftime(" %d/%m %H:%M") if event.get("last_result_epoch") else ""
        wifi = f"conectado ({event['rssi']} dBm)" if event["wifi_connected"] else \
            ("desconectado" if event["wifi_configured"] else "não configurado")
        self.info.set(f"Hora: {event['clock_source']} | Câmera: {event['camera_ready']} | Calibrado: {event['calibrated']} | "
                      f"Wi-Fi: {wifi} | Telegram: {'ok' if event['telegram_configured'] else 'não configurado'}"
                      f" (pendentes {event['notify_pending']}) | Agenda: {s['hour']:02}:{s['minute']:02}, "
                      f"janela {s['window_s']} s{'' if s['enabled'] else ' (DESATIVADA)'} | "
                      f"Último resultado: {STATES.get(last, last) if last != 'idle' else '—'}{when}")

    def poll(self):
        while not self.events.empty():
            event, raw = self.events.get_nowait()
            if not event:
                self.write(raw); continue
            kind = event["event"]
            if kind == "disconnected":
                self.disconnect(); self.write("FALHA USB: " + raw); break
            self.last_contact = time.monotonic()
            if kind == "image":
                try:
                    self.image_data = pgm_image(event)
                    self.photo = tk.PhotoImage(data=self.image_data, format="PPM").zoom(3)
                    self.canvas.delete("frame")
                    self.canvas.create_image(0, 0, image=self.photo, anchor="nw", tags="frame")
                    self.draw_roi()
                except (ValueError, tk.TclError) as exc:
                    self.write("Imagem inválida: " + str(exc))
            else:
                self.write(raw)
            if kind == "reply":
                self.pending.pop(event.get("id"), None)
                if not event.get("ok"):
                    self.info.set("ERRO: " + event.get("message", ""))
            if kind == "status":
                self.show_status(event)
        now = time.monotonic()
        if self.link and now - self.last_contact > 25:
            self.state.set("SEM RESPOSTA DA PLACA — verifique USB e alimentação")
        for seq, (cmd, sent) in list(self.pending.items()):
            if now - sent > 30:
                self.write(f"Sem confirmação de {cmd} (id {seq}); consulte Estado antes de repetir.")
                del self.pending[seq]
        self.root.after(100, self.poll)

    def close(self):
        self.disconnect(); self.root.destroy()


if __name__ == "__main__":
    root = tk.Tk()
    Panel(root)
    root.mainloop()
