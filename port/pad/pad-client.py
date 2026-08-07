#!/usr/bin/env python3
"""GOF2HD virtual gamepad client (run on PC).

Reads local keyboard and sends Linux input-code events over UDP to the
pad-server daemon on the device, which injects them into a virtual uinput
gamepad. The host then converts gamepad events into touch / back events.

Keys:
    Arrows / WASD      D-pad up/down/left/right
    Enter / Space / J  BTN_A  (tap)
    Backspace / K      BTN_B  (host: BackButtonPressed)
    Esc                BTN_SELECT
    Tab                BTN_START
    Q / E              BTN_TL / BTN_TR
"""
import argparse, os, select, socket, sys, termios, tty

DPAD = {'up': 0x220, 'down': 0x221, 'left': 0x222, 'right': 0x223}
BTN = {'a': 0x130, 'b': 0x131, 'x': 0x133, 'y': 0x134,
       'select': 0x13a, 'start': 0x13b, 'tl': 0x136, 'tr': 0x137}
# map -> (code); arrows arrive as ESC [ A/B/C/D
ESC_SEQ = {
    b'\x1b[A': 'up', b'\x1b[B': 'down', b'\x1b[C': 'right', b'\x1b[D': 'left',
}
KEYMAP = {
    'w': 'up', 's': 'down', 'a': 'left', 'd': 'right',
    '\r': 'a', ' ': 'a', 'j': 'a',
    '\x7f': 'b', 'k': 'b',
    '\x1b': 'select', '\t': 'start',
    'q': 'tl', 'e': 'tr', 'i': 'x', 'o': 'y',
}

def main():
    ap = argparse.ArgumentParser()
    ap.add_argument('--host', default='192.168.0.128', help='device IP')
    ap.add_argument('--port', type=int, default=4444)
    a = ap.parse_args()
    s = socket.socket(socket.AF_INET, socket.SOCK_DGRAM)

    def send(code, val):
        s.sendto(('KEY %d %d\n' % (code, val)).encode(), (a.host, a.port))
        print('-> %s %s' % ('KEY %d' % code, val), flush=True)

    fd = sys.stdin.fileno()
    old = termios.tcgetattr(fd)
    try:
        tty.setraw(fd)
        buf = b''
        print('pad-client: keys->virtual gamepad, Ctrl+C to quit', flush=True)
        while True:
            r, _, _ = select.select([sys.stdin], [], [], 0.2)
            if not r:
                continue
            ch = os.read(fd, 1)
            buf += ch
            # try escape sequence match
            if buf in ESC_SEQ:
                name = ESC_SEQ[buf]; send(DPAD[name], 1); send(DPAD[name], 0); buf = b''
            elif buf.startswith(b'\x1b['):
                if len(buf) >= 3:
                    buf = buf[1:]  # malformed, drop esc
            else:
                if buf in (b'\r', b' ', b'\x7f', b'\x1b', b'\t') or len(buf) == 1:
                    name = KEYMAP.get(buf.decode('latin1'))
                    if name in BTN:
                        send(BTN[name], 1); send(BTN[name], 0)
                    elif name in DPAD:
                        send(DPAD[name], 1); send(DPAD[name], 0)
                    buf = b''
                elif len(buf) > 4:
                    buf = b''
    finally:
        termios.tcsetattr(fd, termios.TCSADRAIN, old)
        s.close()

if __name__ == '__main__':
    main()
