#!/usr/bin/env python3
# Lit le port serie de diagnostic de la carte (DTR active, brut) pendant N secondes.
# Usage : python3 tools/serial_capture.py /dev/cu.usbmodemXXXX 30 captures/debug_log.txt
import os, sys, fcntl, termios, struct, time, tty, select
port, secs, out = sys.argv[1], float(sys.argv[2]), sys.argv[3]
fd = os.open(port, os.O_RDWR | os.O_NOCTTY | os.O_NONBLOCK)
tty.setraw(fd)
fcntl.ioctl(fd, termios.TIOCMBIS, struct.pack('I', termios.TIOCM_DTR | termios.TIOCM_RTS))
end = time.time() + secs
n = 0
with open(out, 'wb') as f:
    while time.time() < end:
        r, _, _ = select.select([fd], [], [], 0.5)
        if r:
            try:
                d = os.read(fd, 4096)
            except BlockingIOError:
                continue
            if d:
                f.write(d); n += len(d)
print("octets", n)
