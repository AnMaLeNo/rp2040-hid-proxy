#!/bin/zsh
# Envoie une commande a la carte de controle (produit USB "proxy-ctrl") et affiche sa reponse.
# Usage : tools/ctrl.sh g|r|b|?
# Le port serie est retrouve par le nom de produit USB, pas par /dev/cu.usbmodem* (le proxy
# en mode dev expose aussi un port serie).
CMD=${1:-?}
TTY=$(ioreg -r -n "proxy-ctrl" -l -w0 2>/dev/null | grep -o '"IOCalloutDevice" = "[^"]*"' | head -1 | sed 's/.*= "//; s/"$//')
if [ -z "$TTY" ]; then
  echo "carte de controle introuvable (produit USB proxy-ctrl)" >&2
  exit 1
fi
python3 - "$TTY" "$CMD" <<'PY'
import os, sys, tty, fcntl, termios, struct, time, select
fd = os.open(sys.argv[1], os.O_RDWR | os.O_NOCTTY | os.O_NONBLOCK)
tty.setraw(fd)
fcntl.ioctl(fd, termios.TIOCMBIS, struct.pack('I', termios.TIOCM_DTR | termios.TIOCM_RTS))
time.sleep(0.2)
os.write(fd, sys.argv[2].encode())
buf = b''
end = time.time() + (5.5 if sys.argv[2] == 'g' else 1.5)
while time.time() < end:
    if select.select([fd], [], [], 0.1)[0]:
        try:
            buf += os.read(fd, 4096)
        except BlockingIOError:
            pass
        if b'\n' in buf:
            break
print(buf.decode(errors='replace').strip())
PY
