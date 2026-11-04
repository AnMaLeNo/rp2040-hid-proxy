"""Windows : fenêtre-cible qui reçoit les clics du proxy pendant un test, pour qu'aucun clic ne tombe sur une autre application.

    python tools/win_click_sink.py [secondes=30] [--send N] [--gap 1.2] [--first 1.0]

Ouvre une petite fenêtre toujours au premier plan au centre de l'écran, y place le pointeur, puis compte pendant `secondes` :
  * les appuis / relâchements du bouton gauche reçus par la fenêtre (horloge epoch, comparable à celle du Mac) ;
  * les inclinaisons de molette (défilement horizontal) : c'est ce qui ouvre le filtre du proxy (voir README, « Clic filtré par la molette »).
Avec `--send N`, envoie N commandes `k` (impulsion de clic de 60 ms de la carte de contrôle) par SSH sur le Mac, espacées de `--gap` s (le premier après `--first` s : le temps d'incliner la molette), et note l'heure de chaque envoi.
À la fin : tableau des envois, des clics reçus et des inclinaisons vues. Ne pas toucher la souris (sauf l'inclinaison demandée) : le pointeur doit rester dans la fenêtre.
Variables d'environnement : CTRL_SSH (défaut antoine@192.168.1.127), CTRL_SSH_KEY (défaut ~/.ssh/id_ed25519_claude), CTRL_REPO (défaut ~/rp2040-hid-proxy), comme win_reflash.py.
"""
import ctypes
import os
import subprocess
import sys
import time
import tkinter as tk


def main():
    args = [a for a in sys.argv[1:] if not a.startswith("--")]
    secs = float(args[0]) if args else 30.0
    n_send = int(sys.argv[sys.argv.index("--send") + 1]) if "--send" in sys.argv else 0
    gap = float(sys.argv[sys.argv.index("--gap") + 1]) if "--gap" in sys.argv else 1.2
    first = float(sys.argv[sys.argv.index("--first") + 1]) if "--first" in sys.argv else 1.0
    host = os.environ.get("CTRL_SSH", "antoine@192.168.1.127")
    key = os.environ.get("CTRL_SSH_KEY", os.path.expanduser("~/.ssh/id_ed25519_claude"))
    repo = os.environ.get("CTRL_REPO", "~/rp2040-hid-proxy")

    root = tk.Tk()
    root.title("Cible de test du proxy")
    root.attributes("-topmost", True)
    w, h = 520, 300
    sw, sh = root.winfo_screenwidth(), root.winfo_screenheight()
    root.geometry(f"{w}x{h}+{(sw - w) // 2}+{(sh - h) // 2}")
    label = tk.Label(root, text="cible de test : ne touche pas la souris", font=("Arial", 18))
    label.pack(expand=True, fill="both")
    downs, ups, tilts, sent = [], [], [], []
    root.bind("<ButtonPress-1>", lambda e: downs.append(time.time()))
    root.bind("<ButtonRelease-1>", lambda e: ups.append(time.time()))
    root.bind("<MouseWheel>", lambda e: tilts.append((time.time(), e.delta, bool(e.state & 1))))   # inclinaison = molette avec l'état Maj (Tk 8.6)
    root.update()
    ctypes.windll.user32.SetCursorPos(sw // 2, sh // 2)
    root.update()
    t0 = time.time()
    print(f"cible prête (epoch {t0:.3f}), {secs:g} s, {n_send} envoi(s) de `k`", flush=True)
    t_end = t0 + secs
    next_send = t0 + first
    while time.time() < t_end:
        if len(sent) < n_send and time.time() >= next_send:
            ts = time.time()
            r = subprocess.run(["ssh", "-i", key, host, f"cd {repo} && ./tools/ctrl.sh k"], capture_output=True, text=True)
            sent.append((ts, time.time(), r.stdout.strip() + r.stderr.strip()))
            next_send = time.time() + gap
        root.update()
        time.sleep(0.002)
    root.destroy()
    print(f"envois de k : {len(sent)}")
    for ts, te, out in sent:
        print(f"  {ts:.3f} -> {te:.3f}  {out[:80]}")
    print(f"appuis reçus : {len(downs)}  relâchements : {len(ups)}")
    for t in downs:
        print(f"  appui {t:.3f}")
    print(f"inclinaisons vues : {sum(1 for t in tilts if t[2])} évènements avec Maj, {len(tilts)} molette au total")
    if tilts:
        print(f"  première {tilts[0][0]:.3f}, dernière {tilts[-1][0]:.3f}")


if __name__ == "__main__":
    sys.stdout.reconfigure(encoding="utf-8")
    main()
