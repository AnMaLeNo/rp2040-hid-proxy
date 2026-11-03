#!/bin/zsh
# Envoie une commande a la carte de controle (HID vendeur 2E8A:10C1, outil tools/ctrlhid).
# Commandes : g (BOOTSEL du proxy par GP2, 4 s), r (reset RUN), b (BOOTSEL par les boutons),
#             P / R / k (clic par le fil : appui / relache / impulsion 60 ms),
#             B (la carte de controle repasse en BOOTSEL), ? (etat des lignes).
# Usage : tools/ctrl.sh <cmd>
set -e
CMD=${1:-?}
HERE=$(cd "$(dirname "$0")" && pwd)
if [ ! -x "$HERE/ctrlhid" ]; then
  clang -O2 -o "$HERE/ctrlhid" "$HERE/ctrlhid.c" -framework IOKit -framework CoreFoundation
fi
exec "$HERE/ctrlhid" "$CMD"
