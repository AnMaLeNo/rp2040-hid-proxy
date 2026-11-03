"""Windows : observe les évènements souris BRUTS (Raw Input) par appareil, pour voir ce qui arrive réellement du proxy.

    python tools/win_mouse_watch.py [secondes=10] [--only c08b]       --only : filtre sur un morceau du nom d'appareil (VID/PID)

Équivalent Windows de `button_watch` (macOS). Affiche chaque changement de bouton avec l'horloge `time.time()` (epoch, comparable à celle du Mac à l'erreur de synchronisation près)
et, par appareil : nombre de rapports de déplacement, somme des déplacements. Fenêtre invisible (message-only) avec RIDEV_INPUTSINK : les évènements sont vus même sans le focus.
"""
import ctypes
import ctypes.wintypes as wt
import sys
import time

u32, k32 = ctypes.windll.user32, ctypes.windll.kernel32
LRESULT = ctypes.c_ssize_t
WNDPROC = ctypes.WINFUNCTYPE(LRESULT, wt.HWND, wt.UINT, wt.WPARAM, wt.LPARAM)
WM_INPUT, RIDEV_INPUTSINK, RID_INPUT, RIDI_DEVICENAME = 0x00FF, 0x100, 0x10000003, 0x20000007
HWND_MESSAGE = wt.HWND(-3)
BTN = {0x1: "gauche enfoncé", 0x2: "gauche relâché", 0x4: "droit enfoncé", 0x8: "droit relâché", 0x10: "milieu enfoncé", 0x20: "milieu relâché",
       0x40: "bouton4 enfoncé", 0x80: "bouton4 relâché", 0x100: "bouton5 enfoncé", 0x200: "bouton5 relâché", 0x400: "molette"}


class RAWINPUTDEVICE(ctypes.Structure):
    _fields_ = [("usUsagePage", wt.USHORT), ("usUsage", wt.USHORT), ("dwFlags", wt.DWORD), ("hwndTarget", wt.HWND)]


class RAWINPUTHEADER(ctypes.Structure):
    _fields_ = [("dwType", wt.DWORD), ("dwSize", wt.DWORD), ("hDevice", wt.HANDLE), ("wParam", wt.WPARAM)]


class RAWMOUSE(ctypes.Structure):
    # l'union ulButtons / (usButtonFlags, usButtonData) est alignée sur 4 octets : 2 octets de remplissage après usFlags
    _fields_ = [("usFlags", wt.USHORT), ("_pad", wt.USHORT), ("usButtonFlags", wt.USHORT), ("usButtonData", wt.USHORT), ("ulRawButtons", wt.ULONG),
                ("lLastX", wt.LONG), ("lLastY", wt.LONG), ("ulExtraInformation", wt.ULONG)]


class WNDCLASSW(ctypes.Structure):
    _fields_ = [("style", wt.UINT), ("lpfnWndProc", WNDPROC), ("cbClsExtra", ctypes.c_int), ("cbWndExtra", ctypes.c_int), ("hInstance", wt.HANDLE),
                ("hIcon", wt.HANDLE), ("hCursor", wt.HANDLE), ("hbrBackground", wt.HANDLE), ("lpszMenuName", wt.LPCWSTR), ("lpszClassName", wt.LPCWSTR)]


u32.DefWindowProcW.argtypes = [wt.HWND, wt.UINT, wt.WPARAM, wt.LPARAM]
u32.DefWindowProcW.restype = LRESULT
u32.GetRawInputData.argtypes = [wt.HANDLE, wt.UINT, ctypes.c_void_p, ctypes.POINTER(wt.UINT), wt.UINT]
u32.GetRawInputDeviceInfoW.argtypes = [wt.HANDLE, wt.UINT, ctypes.c_void_p, ctypes.POINTER(wt.UINT)]
u32.CreateWindowExW.restype = wt.HWND
u32.CreateWindowExW.argtypes = [wt.DWORD, wt.LPCWSTR, wt.LPCWSTR, wt.DWORD, ctypes.c_int, ctypes.c_int, ctypes.c_int, ctypes.c_int, wt.HWND, wt.HANDLE, wt.HANDLE, ctypes.c_void_p]

names, stats, log = {}, {}, []


def dev_name(h):
    if h not in names:
        n = wt.UINT(0)
        u32.GetRawInputDeviceInfoW(h, RIDI_DEVICENAME, None, ctypes.byref(n))
        buf = ctypes.create_unicode_buffer(n.value + 1)
        u32.GetRawInputDeviceInfoW(h, RIDI_DEVICENAME, buf, ctypes.byref(n))
        names[h] = buf.value
    return names[h]


def on_input(lparam):
    size = wt.UINT(0)
    u32.GetRawInputData(lparam, RID_INPUT, None, ctypes.byref(size), ctypes.sizeof(RAWINPUTHEADER))
    buf = ctypes.create_string_buffer(size.value)
    if u32.GetRawInputData(lparam, RID_INPUT, buf, ctypes.byref(size), ctypes.sizeof(RAWINPUTHEADER)) != size.value:
        return
    hdr = RAWINPUTHEADER.from_buffer_copy(buf)
    if hdr.dwType != 0:      # 0 = souris
        return
    m = RAWMOUSE.from_buffer_copy(buf, ctypes.sizeof(RAWINPUTHEADER))
    name = dev_name(hdr.hDevice)
    st = stats.setdefault(name, {"reports": 0, "moves": 0, "dx": 0, "dy": 0, "buttons": 0})
    st["reports"] += 1
    if m.lLastX or m.lLastY:
        st["moves"] += 1
        st["dx"] += m.lLastX
        st["dy"] += m.lLastY
    for bit, label in BTN.items():
        if m.usButtonFlags & bit and bit != 0x400:
            st["buttons"] += 1
            log.append((time.time(), name, label))


@WNDPROC
def wndproc(hwnd, msg, wparam, lparam):
    if msg == WM_INPUT:
        try:
            on_input(lparam)
        except Exception as e:     # noqa: BLE001
            print("erreur :", e, file=sys.stderr)
        return 0
    return u32.DefWindowProcW(hwnd, msg, wparam, lparam)


def main():
    secs = float(next((a for a in sys.argv[1:] if not a.startswith("-")), 10))
    only = sys.argv[sys.argv.index("--only") + 1].lower() if "--only" in sys.argv else ""
    wc = WNDCLASSW()
    wc.lpfnWndProc, wc.hInstance, wc.lpszClassName = wndproc, k32.GetModuleHandleW(None), "MouseWatch"
    u32.RegisterClassW(ctypes.byref(wc))
    hwnd = u32.CreateWindowExW(0, "MouseWatch", "MouseWatch", 0, 0, 0, 0, 0, HWND_MESSAGE, None, wc.hInstance, None)
    rid = RAWINPUTDEVICE(1, 2, RIDEV_INPUTSINK, hwnd)
    if not u32.RegisterRawInputDevices(ctypes.byref(rid), 1, ctypes.sizeof(rid)):
        sys.exit("RegisterRawInputDevices a échoué")
    print(f"observation des évènements souris bruts pendant {secs:g} s (epoch de départ {time.time():.3f})", flush=True)
    t_end, msg = time.monotonic() + secs, wt.MSG()
    while time.monotonic() < t_end:
        while u32.PeekMessageW(ctypes.byref(msg), None, 0, 0, 1):
            u32.TranslateMessage(ctypes.byref(msg))
            u32.DispatchMessageW(ctypes.byref(msg))
        time.sleep(0.0005)
    for name, st in stats.items():
        if only and only not in name.lower():
            continue
        print(f"{name}\n    rapports {st['reports']} · avec déplacement {st['moves']} (Σdx {st['dx']}, Σdy {st['dy']}) · changements de bouton {st['buttons']}")
    for t, name, label in log:
        if not only or only in name.lower():
            print(f"  {t:.4f}  {label}  [{name.split('#')[1] if '#' in name else name}]")
    if not stats:
        print("aucun évènement souris reçu")


if __name__ == "__main__":
    main()
