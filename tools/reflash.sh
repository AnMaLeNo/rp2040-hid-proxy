#!/bin/zsh
# Reflashe la carte sans la debrancher : envoie 'B' au port serie (firmware debug), attend RPI-RP2, copie le .uf2.
# Usage : tools/reflash.sh firmware/xxx.uf2 [secondes_de_capture] [fichier_log]
UF2=$1; SECS=${2:-40}; LOG=${3:-captures/last.txt}
P=$(ls /dev/cu.usbmodem* 2>/dev/null | head -1)
if [ -n "$P" ] && [ ! -d /Volumes/RPI-RP2 ]; then
  python3 - "$P" <<'PY'
import os,sys,tty,fcntl,termios,struct,time
fd=os.open(sys.argv[1], os.O_RDWR|os.O_NOCTTY|os.O_NONBLOCK); tty.setraw(fd)
fcntl.ioctl(fd, termios.TIOCMBIS, struct.pack('I', termios.TIOCM_DTR)); time.sleep(0.2)
os.write(fd, b'B'); time.sleep(0.2)
PY
fi
for i in $(seq 1 20); do [ -d /Volumes/RPI-RP2 ] && break; sleep 1; done
[ -d /Volumes/RPI-RP2 ] || { echo "pas de RPI-RP2"; exit 1; }
cp "$UF2" /Volumes/RPI-RP2/ && echo "flash ok: $UF2"
sleep 8
P=$(ls /dev/cu.usbmodem* 2>/dev/null | head -1); echo "port=$P"
[ -n "$P" ] && python3 tools/serial_capture.py "$P" "$SECS" "$LOG"
