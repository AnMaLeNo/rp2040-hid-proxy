"""Windows : reflashe le proxy sans toucher à la carte, puis capture son port série (variantes `_dev`).

    python tools/win_reflash.py firmware/proxy_souris_click_dev.uf2 [--secs 20] [--log captures/x.txt]

Équivalent Windows de `tools/reflash.sh` (macOS). Le proxy est branché à CE PC ; la carte de contrôle reste branchée au Mac et se pilote en SSH :
  1. si le proxy est déjà en BOOTSEL (disque RPI-RP2) : rien à demander ;
  2. s'il expose un port série (variantes debug / dev) : envoie `B` ;
  3. sinon : `ctrl.sh g` sur le Mac (GP2 à 0 pendant 4 s : sur la variante `click`, un clic de 2 s puis BOOTSEL).
Puis copie du .uf2 sur le disque, attente du redémarrage, et capture du port série pendant --secs (0 = pas de capture).
Variables : CTRL_SSH (défaut antoine@192.168.1.127), CTRL_SSH_KEY (défaut ~/.ssh/id_ed25519_claude), CTRL_REPO (défaut ~/rp2040-hid-proxy, sur le Mac).
"""
import argparse
import ctypes
import os
import shutil
import string
import subprocess
import sys
import time

import serial
import serial.tools.list_ports

CTRL_SSH = os.environ.get("CTRL_SSH", "antoine@192.168.1.127")
CTRL_KEY = os.environ.get("CTRL_SSH_KEY", os.path.expanduser("~/.ssh/id_ed25519_claude"))
CTRL_REPO = os.environ.get("CTRL_REPO", "~/rp2040-hid-proxy")


def boot_drive():
    """Lettre du disque RPI-RP2, ou None."""
    mask = ctypes.windll.kernel32.GetLogicalDrives()
    for i, letter in enumerate(string.ascii_uppercase):
        if mask >> i & 1:
            buf = ctypes.create_unicode_buffer(261)
            if ctypes.windll.kernel32.GetVolumeInformationW(f"{letter}:\\", buf, 261, None, None, None, None, 0) and buf.value == "RPI-RP2":
                return f"{letter}:\\"
    return None


def proxy_port():
    for p in serial.tools.list_ports.comports():
        if p.vid in (0x046D, 0x2E8A):
            return p.device
    return None


def ctrl(cmd):
    r = subprocess.run(["ssh", "-i", CTRL_KEY, "-o", "BatchMode=yes", "-o", "ConnectTimeout=8", CTRL_SSH, f"cd {CTRL_REPO} && ./tools/ctrl.sh {cmd}"],
                       capture_output=True, text=True, timeout=30)
    return r.stdout.strip() + r.stderr.strip()


def main():
    ap = argparse.ArgumentParser(description=__doc__.splitlines()[0])
    ap.add_argument("uf2")
    ap.add_argument("--secs", type=float, default=20)
    ap.add_argument("--log", default="")
    a = ap.parse_args()
    if not os.path.exists(a.uf2):
        sys.exit("uf2 introuvable : " + a.uf2)
    if boot_drive() is None:
        port = proxy_port()
        if port:
            print(f"proxy en mode série sur {port} : commande B")
            with serial.Serial(port, 115200, timeout=0.2, dsrdtr=False) as s:
                s.dtr = True
                time.sleep(0.2)
                s.write(b"B")
                time.sleep(0.3)
        else:
            print("pas de port série du proxy : BOOTSEL par la carte de contrôle (g) ->", ctrl("g"))
    drive = None
    for _ in range(30):
        drive = boot_drive()
        if drive:
            break
        time.sleep(1)
    if not drive:
        sys.exit("pas de disque RPI-RP2 (le proxy n'est pas passé en BOOTSEL)")
    shutil.copyfile(a.uf2, os.path.join(drive, os.path.basename(a.uf2)))
    print("flash ok :", a.uf2)
    for _ in range(30):                       # le disque disparaît quand la carte redémarre
        if boot_drive() is None:
            break
        time.sleep(0.5)
    if a.secs <= 0:
        return
    out = open(a.log, "wb") if a.log else None
    n, t_end, t_last = 0, time.time() + a.secs + 25, 0.0
    first = None
    # La variante _dev présente d'abord le port série seul, puis change d'identité USB quelques secondes plus tard : le port disparaît et revient (parfois sous un autre nom).
    # On (ré)ouvre donc le port dès qu'il existe, jusqu'à avoir capturé --secs secondes de journal.
    while time.time() < t_end and (first is None or time.time() - first < a.secs):
        port = proxy_port()
        if not port:
            time.sleep(0.3)
            continue
        try:
            with serial.Serial(port, 115200, timeout=0.2) as s:
                s.dtr = s.rts = True
                if first is None:
                    first = time.time()
                    print("port :", port, flush=True)
                while time.time() - first < a.secs:
                    d = s.read(4096)
                    if d:
                        n += len(d)
                        (out or sys.stdout.buffer).write(d)
                        (out or sys.stdout.buffer).flush()
        except (serial.SerialException, OSError):
            time.sleep(0.3)
    print("octets", n)


if __name__ == "__main__":
    main()
