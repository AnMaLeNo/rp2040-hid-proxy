#!/bin/zsh
# Reflash du proxy sans toucher a la carte.
# Usage : tools/reflash.sh <uf2> [secondes de capture serie] [fichier log]
# 1. Si le proxy expose un port serie (variantes debug/dev), envoie 'B' (retour BOOTSEL).
# 2. Sinon, demande a la carte de controle de tirer GP3 du proxy a 0 ('g').
# 3. Attend le disque RPI-RP2, copie le uf2, puis capture le port serie du proxy s'il existe.
UF2=$1
SECS=${2:-40}
LOG=${3:-captures/last.txt}
HERE=${0:a:h}

proxy_tty() {
  # port serie du proxy = tout IOCalloutDevice qui n'appartient pas a la carte de controle
  local ctrl=$(ioreg -r -n "proxy-ctrl" -l -w0 2>/dev/null | grep -o '"IOCalloutDevice" = "[^"]*"' | sed 's/.*= "//; s/"$//')
  for p in /dev/cu.usbmodem*(N); do
    [ "$p" = "$ctrl" ] || { echo "$p"; return; }
  done
}

if [ ! -d /Volumes/RPI-RP2 ]; then
  P=$(proxy_tty)
  if [ -n "$P" ]; then
    echo "proxy en mode serie sur $P : commande B"
    python3 - "$P" <<'PY'
import os,sys,tty,fcntl,termios,struct,time
fd=os.open(sys.argv[1], os.O_RDWR|os.O_NOCTTY|os.O_NONBLOCK)
tty.setraw(fd)
fcntl.ioctl(fd, termios.TIOCMBIS, struct.pack('I', termios.TIOCM_DTR))
time.sleep(0.2)
os.write(fd, b'B')
time.sleep(0.2)
PY
  else
    echo "pas de port serie proxy : demande BOOTSEL par la carte de controle"
    "$HERE/ctrl.sh" g || exit 1
  fi
fi

for i in $(seq 1 20); do
  [ -d /Volumes/RPI-RP2 ] && break
  sleep 1
done
[ -d /Volumes/RPI-RP2 ] || { echo "pas de RPI-RP2"; exit 1; }

cp "$UF2" /Volumes/RPI-RP2/ && echo "flash ok: $UF2"
sleep 12   # la variante dev change d identite USB 3 s apres le demarrage

P=$(proxy_tty)
echo "port=$P"
[ -n "$P" ] && [ "$SECS" -gt 0 ] && python3 "$HERE/serial_capture.py" "$P" "$SECS" "$LOG"
exit 0
