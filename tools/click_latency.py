#!/usr/bin/env python3
# Delai d'un clic injecte par le fil : du write('k') sur le port serie de la carte de
# controle jusqu'a l'evenement bouton vu par macOS (tools/button_watch, meme horloge
# epoch). Inclut donc le trajet Mac -> carte de controle (USB CDC) -> fil -> proxy ->
# USB -> macOS. Borne superieure du delai du proxy seul.
# Usage : python3 tools/click_latency.py <port_ctrl> [n] [fichier_sortie]
import os, subprocess, sys, time, re

port = sys.argv[1]
n = int(sys.argv[2]) if len(sys.argv) > 2 else 10
out = sys.argv[3] if len(sys.argv) > 3 else "captures/click_latency.txt"
here = os.path.dirname(os.path.abspath(__file__))
dur = int(n * 1.0 + 3)

watch = subprocess.Popen([os.path.join(here, "button_watch"), str(dur)],
                         stdout=subprocess.PIPE, text=True)
time.sleep(1.0)
fd = os.open(port, os.O_RDWR | os.O_NOCTTY | os.O_NONBLOCK)
writes = []
for i in range(n):
    t = time.time()
    os.write(fd, b"k")
    writes.append(t)
    time.sleep(1.0)
os.close(fd)
text, _ = watch.communicate()
presses = [float(m.group(1)) for m in re.finditer(r"^(\d+\.\d+) bouton gauche PRESSE", text, re.M)]
releases = [float(m.group(1)) for m in re.finditer(r"^(\d+\.\d+) bouton gauche RELACHE", text, re.M)]

lines = []
deltas = []
for t in writes:
    after = [p for p in presses if p >= t and p - t < 0.5]
    if after:
        d = (after[0] - t) * 1000
        deltas.append(d)
        lines.append(f"write {t:.6f} -> presse +{d:.2f} ms")
    else:
        lines.append(f"write {t:.6f} -> aucun appui vu")
rel = [(r - p) * 1000 for p, r in zip(presses, releases)]
summary = (f"clics envoyes={n} vus={len(deltas)} "
           + (f"delai min={min(deltas):.2f} med={sorted(deltas)[len(deltas)//2]:.2f} max={max(deltas):.2f} ms "
              if deltas else "")
           + (f"duree appui min={min(rel):.1f} max={max(rel):.1f} ms" if rel else ""))
lines.append(summary)
with open(out, "w") as f:
    f.write(text)
    f.write("\n".join(lines) + "\n")
print("\n".join(lines))
