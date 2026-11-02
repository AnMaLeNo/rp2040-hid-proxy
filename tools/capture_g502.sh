#!/bin/zsh
# Dump complet des objets USB et HID du Mac (ioreg). Filtrer ensuite sur le vendeur voulu
# (Logitech = "idVendor" = 1133, Keychron = 13364).
# Usage : ./tools/capture_g502.sh > captures/g502_$(date +%Y%m%d_%H%M%S).txt
echo "== system_profiler SPUSBDataType =="
system_profiler SPUSBDataType
echo "== ioreg IOUSBHostDevice (-r : sous-arbre complet de chaque peripherique) =="
ioreg -r -c IOUSBHostDevice -l -w0
echo "== ioreg IOHIDDevice =="
ioreg -r -c IOHIDDevice -l -w0
