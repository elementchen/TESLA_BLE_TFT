#!/usr/bin/env python3
"""
ESP32 Tesla BLE Dashboard — USB Config Tool (GUI / Windows)
============================================================
Connect to ESP32 over USB serial, set VIN / display model / SPI pins,
then save & reboot. Packaged as a standalone exe with PyInstaller.
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

# ── Colors / fonts (Windows dark theme) ─────────────────────
BG   = "#1a1a1a"
CARD = "#252525"
FG   = "#d4d4d4"
HINT = "#666"
ACC  = "#cc2222"
GRN  = "#2ea043"
FONT      = ("Segoe UI", 10)
FONT_SM   = ("Segoe UI", 9)
FONT_TINY = ("Segoe UI", 7)
FONT_BOLD = ("Segoe UI", 11, "bold")
FONT_BIG  = ("Segoe UI", 13)


class ESP32Config:
    def __init__(self):
        self.ser = None
        self.port = None

    def connect(self, port=None):
        if port is None:
            port = self._find()
        if port is None:
            raise Exception("No ESP32 found. Plug in the board and check the USB cable.")
        self.port = port
        self.ser = serial.Serial(port, BAUD, timeout=2)
        time.sleep(1.5)

    def disconnect(self):
        if self.ser:
            self.ser.close()
            self.ser = None

    def _find(self):
        ports = list(serial.tools.list_ports.comports())
        # Espressif native USB (VID 303A) — exact match first
        for p in ports:
            if "303A" in (p.hwid or "").upper():
                return p.device
        for p in ports:
            if any(x in p.description for x in ("CP210", "CH340", "ESP32",
                                                 "USB Serial", "USB-SERIAL", "Silicon Labs")):
                return p.device
        for p in ports:
            if "COM" in p.device:
                return p.device
        return None

    def _send(self, cmd, timeout=2.0):
        self.ser.reset_input_buffer()
        self.ser.write((cmd + "\r\n").encode())
        lines = []
        t0 = time.time()
        while time.time() - t0 < timeout:
            if self.ser.in_waiting:
                try:
                    l = self.ser.readline().decode(errors='replace').strip()
                    if l:
                        lines.append(l)
                except Exception:
                    break
            else:
                time.sleep(0.05)
        return lines

    def get_status(self):
        info = {"vin": "", "model": "", "pins": [""] * 7}
        for l in self._send("STATUS", timeout=1.5):
            l = l.strip()
            if l.startswith("VIN:"):
                v = l.split(":", 1)[1].strip()
                if "(default" not in v:
                    info["vin"] = v
            elif l.startswith("Display:"):
                info["model"] = l.split(":", 1)[1].strip()
            elif l.startswith("SPI Pins:"):
                parts = l.split(":", 1)[1].strip()
                info["pins"] = [p.split("=")[1] for p in parts.split() if "=" in p]
        return info

    def cmd(self, c):
        return self._send(c)

    def reboot(self):
        self._send("SAVE")
        time.sleep(0.3)
        self._send("REBOOT", timeout=0.5)


class DarkButton(tk.Label):
    """Flat dark label-button. Always binds clicks; honors enabled state."""
    def __init__(self, parent, text, command, bg="#333", fg="#ddd",
                 font=FONT, padx=14, enabled=True):
        super().__init__(parent, text=text, bg=bg, fg=fg, font=font,
                         padx=padx, pady=6, cursor="hand2", relief=tk.FLAT)
        self._bg = bg
        self._command = command
        self._enabled = enabled
        self.bind("<Button-1>", self._on_click)
        self.bind("<Enter>", self._on_enter)
        self.bind("<Leave>", self._on_leave)
        self.set_enabled(enabled)

    def _on_click(self, e):
        if self._enabled:
            self._command()

    def _on_enter(self, e):
        if self._enabled:
            self.configure(bg="#555")

    def _on_leave(self, e):
        if self._enabled:
            self.configure(bg=self._bg)

    def set_enabled(self, enabled):
        self._enabled = enabled
        self.configure(fg="#ddd" if enabled else "#444",
                       cursor="hand2" if enabled else "arrow",
                       bg=self._bg)


class App:
    def __init__(self, root):
        root.title("ESP32 Config Tool")
        root.geometry("760x540")
        root.minsize(760, 540)
        root.configure(bg=BG)
        self._center_window(root, 760, 540)
        self.esp = ESP32Config()
        self.connected = False

        self._style_combobox(root)

        # ── Top bar ──────────────────────────────────────────
        top = tk.Frame(root, bg=CARD, height=44)
        top.pack(fill=tk.X, padx=10, pady=(10, 0))
        top.pack_propagate(False)

        self.led = tk.Canvas(top, width=10, height=10, bg=CARD, highlightthickness=0)
        self.led.place(x=12, y=17)
        self._led("gray")

        self.lbl_status = tk.Label(top, text="Disconnected", fg="#888", bg=CARD, font=FONT)
        self.lbl_status.place(x=30, y=12)
        self.lbl_port = tk.Label(top, text="", fg=HINT, bg=CARD, font=FONT_TINY)
        self.lbl_port.place(x=30, y=30)

        # Port selector (manual override for auto-detect)
        self.port_list = []  # [(device, label, hwid), ...]
        self._user_selected_port = False
        self.port_cb = ttk.Combobox(top, state="readonly", width=27,
                                    font=FONT_SM, postcommand=self._refresh_ports)
        self.port_cb.place(x=380, y=8)
        self.port_cb.bind("<<ComboboxSelected>>", self._on_port_selected)
        self._refresh_ports()

        self.btn_refresh = DarkButton(top, "↻", self._refresh_ports,
                                      font=FONT, padx=8)
        self.btn_refresh.place(x=588, y=6)

        self.btn_conn = DarkButton(top, "Connect", self._toggle)
        self.btn_conn.place(x=660, y=6)

        # ── VIN (always editable, top priority) ─────────────
        vf = tk.Frame(root, bg=CARD)
        vf.pack(fill=tk.X, padx=10, pady=(8, 2))
        tk.Label(vf, text="VIN  (17 characters, required)", fg=FG, bg=CARD,
                 font=FONT_BOLD).pack(anchor=tk.W, padx=12, pady=(8, 0))
        self.vin_var = tk.StringVar()
        tk.Entry(vf, textvariable=self.vin_var, bg="#111", fg=FG, insertbackground=FG,
                 font=FONT_BIG, relief=tk.FLAT, width=28, highlightthickness=0
                 ).pack(anchor=tk.W, padx=12, pady=(2, 8))

        # ── Board preset ─────────────────────────────────────
        bf = tk.Frame(root, bg=CARD)
        bf.pack(fill=tk.X, padx=10, pady=2)
        tk.Label(bf, text="Board", fg=FG, bg=CARD, font=FONT_BOLD).pack(
            side=tk.LEFT, padx=(12, 4), pady=6)

        preset_keys = list(PRESETS.keys()) + [CUSTOM_LABEL]
        self.board_var = tk.StringVar(value=preset_keys[0])
        self.board_cb = ttk.Combobox(bf, textvariable=self.board_var, values=preset_keys,
                                     state="readonly", width=32, font=FONT)
        self.board_cb.pack(side=tk.LEFT, padx=4, pady=6)
        self.board_cb.bind("<<ComboboxSelected>>", self._on_board_select)

        # ── Pins panel ───────────────────────────────────────
        pf = tk.Frame(root, bg=CARD)
        pf.pack(fill=tk.BOTH, expand=True, padx=10, pady=2)

        tk.Label(pf, text="Display Driver", fg=FG, bg=CARD, font=("Segoe UI", 10, "bold")
                 ).place(x=14, y=8)
        self.model_var = tk.StringVar(value="ILI9341")
        self.model_entry = tk.Entry(pf, textvariable=self.model_var, bg=BG, fg=FG,
                                    insertbackground=FG, font=("Segoe UI", 12),
                                    relief=tk.FLAT, width=18, highlightthickness=0)
        self.model_entry.place(x=14, y=32)

        tk.Label(pf, text="SPI Pins  (-1 = not connected)", fg=FG, bg=CARD,
                 font=("Segoe UI", 10, "bold")).place(x=14, y=74)

        self.pin_vars = []
        self.pin_entries = []
        defaults = ["12", "11", "13", "46", "10", "-1", "45"]
        for i, (primary, aliases) in enumerate(PIN_NAMES):
            x = 14 + i * 102
            tk.Label(pf, text=primary, fg=FG, bg=CARD, font=("Segoe UI", 10, "bold")
                     ).place(x=x, y=102)
            tk.Label(pf, text=aliases, fg=HINT, bg=CARD, font=FONT_TINY
                     ).place(x=x, y=122)
            var = tk.StringVar(value=defaults[i])
            self.pin_vars.append(var)
            e = tk.Entry(pf, textvariable=var, bg=BG, fg=FG, insertbackground=FG,
                         font=("Segoe UI", 12), relief=tk.FLAT, width=5,
                         justify=tk.CENTER, highlightthickness=0)
            e.place(x=x, y=142)
            self.pin_entries.append(e)

        # ── Bottom bar ───────────────────────────────────────
        bbar = tk.Frame(root, bg=CARD, height=56)
        bbar.pack(fill=tk.X, padx=10, pady=(4, 10))
        bbar.pack_propagate(False)

        self.log_var = tk.StringVar(value="Ready — connect ESP32 to begin")
        tk.Label(bbar, textvariable=self.log_var, fg=HINT, bg=CARD,
                 font=FONT_SM).place(x=14, y=18)

        self.btn_reboot = DarkButton(bbar, "Save & Reboot", self._reboot, bg=ACC, fg="white",
                                     font=("Segoe UI", 12, "bold"), padx=18, enabled=False)
        self.btn_reboot.place(x=600, y=8)

        # Apply the initially selected preset
        self._on_board_select()

    # ── Helpers ─────────────────────────────────────────────
    def _center_window(self, root, w, h):
        x = max((root.winfo_screenwidth() - w) // 2, 0)
        y = max((root.winfo_screenheight() - h) // 2 - 20, 0)
        root.geometry(f"{w}x{h}+{x}+{y}")

    def _style_combobox(self, root):
        style = ttk.Style(root)
        try:
            style.theme_use("clam")
        except tk.TclError:
            pass
        style.configure("TCombobox",
                        fieldbackground="#111", background="#333",
                        foreground=FG, arrowcolor=FG, bordercolor="#333",
                        selectbackground="#333", selectforeground=FG,
                        lightcolor="#333", darkcolor="#333")
        style.map("TCombobox",
                  fieldbackground=[("readonly", "#111")],
                  foreground=[("readonly", FG)],
                  selectbackground=[("readonly", "#333")],
                  selectforeground=[("readonly", FG)])
        # Popdown list: clam renders it as an internal Listbox; force dark
        # background with light text so items stay readable
        root.option_add("*TCombobox*Listbox.background", CARD)
        root.option_add("*TCombobox*Listbox.foreground", FG)
        root.option_add("*TCombobox*Listbox.selectBackground", "#3a3a3a")
        root.option_add("*TCombobox*Listbox.selectForeground", "#ffffff")
        root.option_add("*TCombobox*Listbox.borderWidth", "1")

    def _led(self, color):
        self.led.delete("all")
        self.led.create_oval(1, 1, 9, 9, fill=color, outline="")

    def _set_connected(self, connected, port=""):
        self.connected = connected
        if connected:
            self.lbl_status.config(text="Connected")
            self.lbl_port.config(text=port)
            self._led(GRN)
            self.btn_conn.configure(text="Disconnect")
            self.btn_reboot.set_enabled(True)
            self.port_cb.configure(state="disabled")
            self.btn_refresh.set_enabled(False)
        else:
            self.lbl_status.config(text="Disconnected")
            self.lbl_port.config(text="")
            self._led("gray")
            self.btn_conn.configure(text="Connect")
            self.btn_reboot.set_enabled(False)
            self._refresh_ports()
            self.port_cb.configure(state="readonly")
            self.btn_refresh.set_enabled(True)

    # USB VID prefixes for common USB-UART bridge chips
    CHIP_IDS = [
        ("303A:1001", "ESP32-S3 (USB-JTAG)"),  # Espressif native USB
        ("303A", "ESP32 (USB-JTAG)"),
        ("10C4", "CP210x"),                    # Silicon Labs
        ("1A86", "CH340"),                     # WCH
        ("0403", "FTDI"),
    ]
    CHIP_PRIORITY = ["ESP32", "CP210", "CH340", "FTDI", "Silicon Labs"]

    def _chip_of(self, hwid):
        hwid = (hwid or "").upper()
        for vid, name in self.CHIP_IDS:
            if vid in hwid:
                return name
        return None

    def _port_label(self, p):
        chip = self._chip_of(p.hwid)
        desc = (p.description or "").strip()
        label = p.device
        if chip:
            label += f" - {chip}"
        if desc and desc not in label:
            label += f" ({desc})"
        return label

    def _on_port_selected(self, event=None):
        self._user_selected_port = True

    def _refresh_ports(self):
        """Rescan COM ports; pre-select the ESP32-like one if possible."""
        current = self.port_cb.get()
        ports = list(serial.tools.list_ports.comports())
        self.port_list = [(p.device, self._port_label(p), p.hwid or "")
                          for p in ports if "COM" in p.device]
        labels = [label for _, label, _ in self.port_list]
        self.port_cb.configure(values=labels)

        if self._user_selected_port and current in labels:
            self.port_cb.set(current)
        elif labels:
            # Prefer ESP32-like ports: USB VID match first, then name hints
            default = 0
            for i, (_, label, hwid) in enumerate(self.port_list):
                if self._chip_of(hwid) and self._chip_of(hwid).startswith("ESP32"):
                    default = i
                    break
            else:
                for i, (_, label, _) in enumerate(self.port_list):
                    if any(x in label for x in self.CHIP_PRIORITY):
                        default = i
                        break
            self.port_cb.set(labels[default])
        else:
            self.port_cb.set("")

    def _selected_port(self):
        label = self.port_cb.get()
        for device, l, _ in self.port_list:
            if l == label:
                return device
        return None

    def _set_locked(self, locked):
        """Lock/unlock model + pin fields (read-only for presets)."""
        fg = FG if not locked else "#888"
        state = tk.NORMAL if not locked else tk.DISABLED
        self.model_entry.configure(bg=BG, fg=fg, state=state)
        for e in self.pin_entries:
            e.configure(bg=BG, fg=fg, state=state)

    def _match_preset(self, model, pins):
        """Return preset name if model+pins match, else None."""
        for name, p in PRESETS.items():
            if p["model"].upper() == model.upper() and p["pins"] == pins:
                return name
        return None

    # ── Events ──────────────────────────────────────────────
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
            self.esp.disconnect()
            self._set_connected(False)
        else:
            try:
                self._refresh_ports()
                self.esp.connect(self._selected_port())
                self._set_connected(True, self.esp.port)
                self._auto_read()
            except Exception as e:
                messagebox.showerror("Connection Error", str(e))

    def _auto_read(self):
        """Read config from ESP32, auto-detect preset vs custom."""
        try:
            self.log("Reading...")
            info = self.esp.get_status()
            if info.get("vin"):
                self.vin_var.set(info["vin"])
            model = info.get("model") or "ILI9341"
            pins = info.get("pins")
            if len(pins) != 7:
                pins = ["12", "11", "13", "46", "10", "-1", "45"]

            # Try to match a preset
            matched = self._match_preset(model, pins)
            if matched:
                self.board_var.set(matched)
                self._set_locked(True)
                self.log(f"Detected: {matched}")
            else:
                self.board_var.set(CUSTOM_LABEL)
                self.model_var.set(model)
                for i, v in enumerate(pins):
                    self.pin_vars[i].set(v)
                self._set_locked(False)
                self.log("Custom config detected")
        except Exception as e:
            self.log(f"Error: {e}")

    def _pins_str(self):
        vals = []
        for v in self.pin_vars:
            s = v.get().strip()
            try:
                int(s)
            except ValueError:
                raise ValueError(f"Invalid pin value: '{s}' (must be an integer, -1 = not connected)")
            vals.append(s)
        return ",".join(vals)

    def _reboot(self):
        if not self.connected:
            return
        if not messagebox.askyesno("Save & Reboot", "Write config and reboot ESP32?"):
            return
        try:
            vin = self.vin_var.get().strip().upper()
            if len(vin) != 17:
                messagebox.showerror("Error", f"VIN must be exactly 17 characters (got {len(vin)})")
                return
            if not all(c.isalnum() for c in vin):
                messagebox.showerror("Error", "VIN must contain only letters and digits")
                return
            model = self.model_var.get().strip()
            if not model:
                messagebox.showerror("Error", "Display model cannot be empty")
                return
            self.log("Writing config...")
            self.esp.cmd(f"SET VIN={vin}")
            self.esp.cmd(f"SET MODEL={model}")
            self.esp.cmd(f"SET PINS={self._pins_str()}")
            self.esp.reboot()
            self._set_connected(False)
            self.log("Rebooting...")
        except ValueError as e:
            messagebox.showerror("Error", str(e))
        except Exception as e:
            self.log(f"Error: {e}")

    def log(self, msg):
        self.log_var.set(msg)


if __name__ == "__main__":
    root = tk.Tk()
    App(root)
    root.mainloop()
