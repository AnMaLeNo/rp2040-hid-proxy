#!/usr/bin/env python3
# Delai d'un clic injecte par le fil, troncon par troncon :
#   - rt : duree du SET_REPORT Mac -> carte de controle (aller-retour, mesure par ctrlhid)
#   - total : du debut du SET_REPORT a l'evenement bouton vu par macOS (tools/button_watch,
#     meme horloge epoch). Inclut Mac -> carte -> fil -> proxy -> USB -> macOS.
# Usage : python3 tools/click_latency.py [n] [fichier_sortie]
import os, subprocess, sys, re

n = int(sys.argv[1]) if len(sys.argv) > 1 else 10
out = sys.argv[2] if len(sys.argv) > 2 else "captures/click_latency.txt"
here = os.path.dirname(os.path.abspath(__file__))
dur = int(n * 1.0 + 3)

watch = subprocess.Popen([os.path.join(here, "button_watch"), str(dur)], stdout=subprocess.PIPE, text=True)
import time; time.sleep(1.0)
bench = subprocess.run([os.path.join(here, "ctrlhid"), "bench", str(n), "k"], capture_output=True, text=True)
text, _ = watch.communicate()

writes = [(float(m.group(1)), float(m.group(2))) for m in re.finditer(r"^write (\d+\.\d+) rt=(\d+) us", bench.stdout, re.M)]
presses = [float(m.group(1)) for m in re.finditer(r"^(\d+\.\d+) bouton gauche PRESSE", text, re.M)]
releases = [float(m.group(1)) for m in re.finditer(r"^(\d+\.\d+) bouton gauche RELACHE", text, re.M)]

lines, deltas, rts = [], [], []
for t, rt in writes:
    rts.append(rt / 1000)
    after = [p for p in presses if p >= t and p - t < 0.5]
    if after:
        d = (after[0] - t) * 1000
        deltas.append(d)
        lines.append(f"write {t:.6f} rt={rt/1000:.2f} ms -> presse +{d:.2f} ms")
    else:
        lines.append(f"write {t:.6f} rt={rt/1000:.2f} ms -> aucun appui vu")
rel = [(r - p) * 1000 for p, r in zip(presses, releases)]
med = lambda v: sorted(v)[len(v) // 2]
summary = f"clics envoyes={len(writes)} vus={len(deltas)}"
if rts: summary += f" | Mac->carte aller-retour min={min(rts):.2f} med={med(rts):.2f} max={max(rts):.2f} ms"
if deltas: summary += f" | total min={min(deltas):.2f} med={med(deltas):.2f} max={max(deltas):.2f} ms"
if rel: summary += f" | duree appui {min(rel):.1f} a {max(rel):.1f} ms"
lines.append(summary)
with open(out, "w") as f:
    f.write(bench.stdout + text + "\n".join(lines) + "\n")
print("\n".join(lines))
