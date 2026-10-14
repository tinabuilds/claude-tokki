#!/usr/bin/env python3
"""Send Wi-Fi + Bambu printer settings to the board over USB serial.

The Wi-Fi password is typed at a hidden prompt; the printer access code is read
from ~/.config/bambu-lan/access_code. Nothing secret is printed.

    printer_setup.py --port /dev/cu.usbmodem1101 --ip <printer LAN IP> --sn <printer serial> [--ssid NAME]
"""
import argparse, getpass, json, os, subprocess, sys, time
import serial


def current_ssid() -> str:
    try:
        out = subprocess.run(["ipconfig", "getsummary", "en0"], capture_output=True, text=True).stdout
        for line in out.splitlines():
            line = line.strip()
            if line.startswith("SSID :"):
                return line.split(":", 1)[1].strip()
    except OSError:
        pass
    return ""


def main() -> None:
    ap = argparse.ArgumentParser()
    ap.add_argument("--port", required=True)
    ap.add_argument("--ip", required=True)
    ap.add_argument("--sn", required=True)
    ap.add_argument("--ssid", default="")
    a = ap.parse_args()

    ssid = a.ssid or input(f"Wi-Fi name [{current_ssid()}]: ").strip() or current_ssid()
    if not ssid:
        sys.exit("No Wi-Fi name.")
    pw = getpass.getpass(f"Wi-Fi password for {ssid} (hidden): ")
    code = open(os.path.expanduser("~/.config/bambu-lan/access_code")).read().strip()

    line = "printer " + json.dumps({"ssid": ssid, "pass": pw, "ip": a.ip, "sn": a.sn, "code": code},
                                   separators=(",", ":"))
    if len(line) >= 512:
        sys.exit("Settings too long for the board's command buffer.")

    with serial.Serial(a.port, 115200, timeout=0.5) as s:
        s.write((line + "\n").encode())
        s.flush()
        t = time.time()
        buf = b""
        while time.time() - t < 25:
            buf += s.read(1024)
            text = buf.decode(errors="replace")
            for want in ("printer: connected", "mqtt connect failed", "bad config"):
                if want in text:
                    break
            else:
                continue
            break
    for l in buf.decode(errors="replace").splitlines():
        if l.startswith("printer:"):
            print(l)
    print("Done. Check the printer page on the screen.")


if __name__ == "__main__":
    main()
