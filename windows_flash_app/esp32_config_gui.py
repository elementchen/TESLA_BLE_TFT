#!/usr/bin/env python3
"""
ESP32 Tesla BLE Dashboard — USB Config Tool (GUI / macOS)
==========================================================
Requires: pip install pyserial
"""

import time, serial, serial.tools.list_ports
import tkinter as tk
from tkinter import ttk, messagebox

PRESETS = {
    "2.8inch ESP32-S3 (ILI9341)": {
        "model": "ILI9341",
        "pins":  ["12","11","13","46","10","-1","45"],
    },
    "ESP32-S3 ST7789 / ST7789T3": {
        "model": "ST7789",
        "pins":  ["12","11","13","46","10","-1","45"],
    },
}
CUSTOM_LABEL = "Custom Board"
BAUD = 115200

PIN_NAMES = [
    ("SCK",  "SCL / CLK"),
    ("MOSI", "SDA / SDI / DATA"),
    ("MISO", "SDO (may be unused)"),
    ("DC",   "DCX / RS / CD"),
    ("CS",   "CSX / SS"),
    ("RST",  "RES / RESET"),
    ("BLK",  "BL / LED / BACKLIGHT"),
]

class ESP32Config:
    def __init__(self):
        self.ser = None; self.port = None
    def connect(self, port=None):
        if port is None: port = self._find()
        if port is None: raise Exception("No ESP32 found")
        self.port = port
        self.ser = serial.Serial(port, BAUD, timeout=2)
        time.sleep(1.5)
    def disconnect(self):
        if self.ser: self.ser.close(); self.ser = None
    def _find(self):
        for p in serial.tools.list_ports.comports():
            if "usbmodem" in p.device or "usbserial" in p.device: return p.device
            if any(x in p.description for x in ("CP210","CH340","ESP32","USB Serial","Silicon Labs")): return p.device
        ports = list(serial.tools.list_ports.comports())
        return ports[0].device if ports else None
    def _send(self, cmd, wait=0.5):
        self.ser.reset_input_buffer()
        self.ser.write((cmd + "\r\n").encode())
        time.sleep(wait)
        lines = []; t0 = time.time()
        while time.time() - t0 < 2.0:
            if self.ser.in_waiting:
                try:
                    l = self.ser.readline().decode(errors='replace').strip()
                    if l: lines.append(l)
                except: break
            else: time.sleep(0.05)
        return lines
    def get_status(self):
        info = {"vin":"","model":"","pins":[""]*7}
        for l in self._send("STATUS", wait=1.0):
            l = l.strip()
            if l.startswith("VIN:"):
                v = l.split(":",1)[1].strip()
                if "(default" not in v: info["vin"] = v
            elif l.startswith("Display:"): info["model"] = l.split(":",1)[1].strip()
            elif l.startswith("SPI Pins:"):
                parts = l.split(":",1)[1].strip()
                info["pins"] = [p.split("=")[1] for p in parts.split() if "=" in p]
        return info
    def cmd(self, c):  return self._send(c)
    def reboot(self):
        self._send("SAVE"); time.sleep(0.3); self._send("REBOOT", wait=0.3)


def DarkButton(parent, text, command, bg="#333", fg="#ddd",
               font=("Helvetica", 10), padx=14, enabled=True):
    lbl = tk.Label(parent, text=text, bg=bg, fg=fg, font=font,
                   padx=padx, pady=6, cursor="hand2", relief=tk.FLAT)
    if enabled:
        lbl.bind("<Button-1>", lambda e: command())
        lbl.bind("<Enter>", lambda e: lbl.configure(bg="#555"))
        lbl.bind("<Leave>", lambda e: lbl.configure(bg=bg))
    else:
        lbl.configure(fg="#444", cursor="arrow")
    lbl._bg = bg; lbl._enabled = enabled
    return lbl


class App:
    def __init__(self, root):
        root.title("ESP32 Config Tool")
        root.geometry("760x540"); root.minsize(760, 540)
        root.configure(bg="#1a1a1a")
        self.esp = ESP32Config(); self.connected = False

        BG="#1a1a1a"; CARD="#252525"; FG="#d4d4d4"; ACC="#cc2222"
        GRN="#2ea043"; BLU="#4a90d9"; HINT="#666"

        # ── Top bar ──────────────────────────────────────────
        top = tk.Frame(root, bg=CARD, height=44)
        top.pack(fill=tk.X, padx=10, pady=(10,0)); top.pack_propagate(False)

        self.led = tk.Canvas(top, width=10, height=10, bg=CARD, highlightthickness=0)
        self.led.place(x=12, y=17); self._led("gray")

        self.lbl_status = tk.Label(top, text="Disconnected", fg="#888", bg=CARD, font=("Helvetica", 10))
        self.lbl_status.place(x=30, y=12)
        self.lbl_port = tk.Label(top, text="", fg=HINT, bg=CARD, font=("Helvetica", 7))
        self.lbl_port.place(x=30, y=30)

        self.btn_conn = DarkButton(top, "Connect", self._toggle)
        self.btn_conn.place(x=660, y=6)

        # ── VIN (always editable, top priority) ─────────────
        vf = tk.Frame(root, bg=CARD)
        vf.pack(fill=tk.X, padx=10, pady=(8,2))
        tk.Label(vf, text="VIN  (17 characters, required)", fg=FG, bg=CARD,
                font=("Helvetica", 11, "bold")).pack(anchor=tk.W, padx=12, pady=(8,0))
        self.vin_var = tk.StringVar()
        tk.Entry(vf, textvariable=self.vin_var, bg="#111", fg=FG, insertbackground=FG,
                font=("Helvetica", 13), relief=tk.FLAT, width=28, highlightthickness=0
                ).pack(anchor=tk.W, padx=12, pady=(2,8))

        # ── Board preset ─────────────────────────────────────
        bf = tk.Frame(root, bg=CARD)
        bf.pack(fill=tk.X, padx=10, pady=2)
        tk.Label(bf, text="Board", fg=FG, bg=CARD, font=("Helvetica", 11, "bold")).pack(
            side=tk.LEFT, padx=(12,4), pady=6)

        preset_keys = list(PRESETS.keys()) + [CUSTOM_LABEL]
        self.board_var = tk.StringVar(value=preset_keys[0])
        self.board_cb = ttk.Combobox(bf, textvariable=self.board_var, values=preset_keys,
                                      state="readonly", width=32, font=("Helvetica", 10))
        self.board_cb.pack(side=tk.LEFT, padx=4, pady=6)
        self.board_cb.bind("<<ComboboxSelected>>", self._on_board_select)

        # ── Pins panel ───────────────────────────────────────
        pf = tk.Frame(root, bg=CARD)
        pf.pack(fill=tk.BOTH, expand=True, padx=10, pady=2)

        tk.Label(pf, text="Display Driver", fg=FG, bg=CARD, font=("Helvetica", 10, "bold")
                ).place(x=14, y=8)
        self.model_var = tk.StringVar(value="ILI9341")
        self.model_entry = tk.Entry(pf, textvariable=self.model_var, bg="#1a1a1a", fg=FG,
                                     insertbackground=FG, font=("Helvetica", 12),
                                     relief=tk.FLAT, width=18, highlightthickness=0)
        self.model_entry.place(x=14, y=32)

        tk.Label(pf, text="SPI Pins  (-1 = not connected)", fg=FG, bg=CARD,
                font=("Helvetica", 10, "bold")).place(x=14, y=74)

        self.pin_vars = []; self.pin_entries = []
        defaults = ["12","11","13","46","10","-1","45"]
        for i, (primary, aliases) in enumerate(PIN_NAMES):
            x = 14 + i * 102
            tk.Label(pf, text=primary, fg=FG, bg=CARD, font=("Helvetica", 10, "bold")
                    ).place(x=x, y=102)
            tk.Label(pf, text=aliases, fg=HINT, bg=CARD, font=("Helvetica", 7)
                    ).place(x=x, y=122)
            var = tk.StringVar(value=defaults[i])
            self.pin_vars.append(var)
            e = tk.Entry(pf, textvariable=var, bg="#1a1a1a", fg=FG, insertbackground=FG,
                        font=("Helvetica", 12), relief=tk.FLAT, width=5,
                        justify=tk.CENTER, highlightthickness=0)
            e.place(x=x, y=142)
            self.pin_entries.append(e)

        # ── Bottom bar ───────────────────────────────────────
        bbar = tk.Frame(root, bg=CARD, height=56)
        bbar.pack(fill=tk.X, padx=10, pady=(4,10)); bbar.pack_propagate(False)

        self.log_var = tk.StringVar(value="Ready — connect ESP32 to begin")
        tk.Label(bbar, textvariable=self.log_var, fg=HINT, bg=CARD,
                font=("Helvetica", 9)).place(x=14, y=18)

        self.btn_reboot = DarkButton(bbar, "Save & Reboot", self._reboot, bg=ACC, fg="white",
                                      font=("Helvetica", 12, "bold"), padx=18, enabled=False)
        self.btn_reboot.place(x=600, y=8)

    def _led(self, color):
        self.led.delete("all"); self.led.create_oval(1,1,9,9, fill=color, outline="")

    def _set_locked(self, locked):
        """Lock/unlock model + pin fields (read-only for presets)."""
        bg = "#1a1a1a" if not locked else "#1a1a1a"
        fg = FG if not locked else "#888"
        state = tk.NORMAL if not locked else tk.DISABLED
        self.model_entry.configure(bg=bg, fg=fg, state=state)
        for e in self.pin_entries:
            e.configure(bg=bg, fg=fg, state=state)

    def _match_preset(self, model, pins):
        """Return preset name if model+pins match, else None."""
        for name, p in PRESETS.items():
            if p["model"].upper() == model.upper() and p["pins"] == pins:
                return name
        return None

    def _on_board_select(self, event=None):
        name = self.board_var.get()
        if name == CUSTOM_LABEL:
            self._set_locked(False)
        elif name in PRESETS:
            p = PRESETS[name]
            self.model_var.set(p["model"])
            for i, v in enumerate(p["pins"]):
                self.pin_vars[i].set(v)
            self._set_locked(True)

    def _toggle(self):
        if self.connected:
            self.esp.disconnect(); self.connected = False
            self.lbl_status.config(text="Disconnected"); self.lbl_port.config(text="")
            self._led("gray"); self.btn_conn.configure(text="Connect")
            self.btn_reboot.configure(fg="#444", cursor="arrow"); self.btn_reboot._enabled = False
        else:
            try:
                self.esp.connect(); self.connected = True
                self.lbl_status.config(text="Connected"); self.lbl_port.config(text=self.esp.port)
                self._led("#2ea043"); self.btn_conn.configure(text="Disconnect")
                self.btn_reboot.configure(fg="white", cursor="hand2"); self.btn_reboot._enabled = True
                self._auto_read()
            except Exception as e:
                messagebox.showerror("Connection Error", str(e))
        self.btn_conn._cb = self._toggle

    def _auto_read(self):
        """Read config from ESP32, auto-detect preset vs custom."""
        try:
            self.log("Reading...")
            info = self.esp.get_status()
            if info.get("vin"): self.vin_var.set(info["vin"])
            model = info.get("model", "ILI9341")
            pins = info.get("pins", ["12","11","13","46","10","-1","45"])

            # Try to match a preset
            matched = self._match_preset(model, pins)
            if matched:
                self.board_var.set(matched)
                p = PRESETS[matched]
                self.model_var.set(p["model"])
                for i, v in enumerate(p["pins"]): self.pin_vars[i].set(v)
                self._set_locked(True)
                self.log(f"Detected: {matched}")
            else:
                self.board_var.set(CUSTOM_LABEL)
                self.model_var.set(model)
                for i, v in enumerate(pins): self.pin_vars[i].set(v)
                self._set_locked(False)
                self.log("Custom config detected")
        except Exception as e:
            self.log(f"Error: {e}")

    def _pins_str(self):
        return ",".join(v.get().strip() for v in self.pin_vars)

    def _reboot(self):
        if not self.connected: return
        if not messagebox.askyesno("Save & Reboot", "Write config and reboot ESP32?"): return
        try:
            vin = self.vin_var.get().strip()
            if not vin or len(vin) != 17:
                messagebox.showerror("Error", "VIN must be exactly 17 characters"); return
            self.esp.cmd(f"SET VIN={vin}")
            self.esp.cmd(f"SET MODEL={self.model_var.get().strip()}")
            self.esp.cmd(f"SET PINS={self._pins_str()}")
            self.esp.reboot()
            self.connected = False
            self.lbl_status.config(text="Disconnected"); self.lbl_port.config(text="")
            self._led("gray"); self.btn_conn.configure(text="Connect")
            self.btn_reboot.configure(fg="#444", cursor="arrow"); self.btn_reboot._enabled = False
            self.log("Rebooting...")
        except Exception as e:
            self.log(f"Error: {e}")

    def log(self, msg):
        self.log_var.set(msg)


if __name__ == "__main__":
    root = tk.Tk()
    app = App(root)
    app.btn_conn._cb = app._toggle
    app.btn_reboot._cb = app._reboot
    root.mainloop()
