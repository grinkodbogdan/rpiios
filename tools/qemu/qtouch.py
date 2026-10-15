"""Drive QEMU's usb-tablet over QMP like a finger on the 480x800 screen.

Usage: qtouch.py SOCK CMD [CMD...]
  tap X Y            press and release at (X, Y)
  drag X1 Y1 X2 Y2 N press at 1, move to 2 in N steps (0.5 s apart), release
  home               right click
  sleep S
  shot NAME          screendump to out/NAME.png (QEMU started from the repo root)
"""
import json, socket, sys, time
from PIL import Image

W, H = 480, 800

class QMP:
    def __init__(self, path):
        self.s = socket.socket(socket.AF_UNIX)
        self.s.connect(path)
        self.f = self.s.makefile('rw')
        self.f.readline()
        self.cmd('qmp_capabilities')

    def cmd(self, name, **args):
        self.f.write(json.dumps({'execute': name, 'arguments': args}) + '\n')
        self.f.flush()
        while True:
            r = json.loads(self.f.readline())
            if 'return' in r or 'error' in r:
                if 'error' in r:
                    print('QMP error', r['error'])
                return r

    def events(self, evs):
        self.cmd('input-send-event', events=evs)

def pos(x, y):
    return [{'type': 'abs', 'data': {'axis': 'x', 'value': int(x * 0x7fff / (W - 1))}},
            {'type': 'abs', 'data': {'axis': 'y', 'value': int(y * 0x7fff / (H - 1))}}]

def btn(name, down):
    return [{'type': 'btn', 'data': {'down': down, 'button': name}}]

q = QMP(sys.argv[1])
args = sys.argv[2:]
i = 0
while i < len(args):
    c = args[i].split()
    op = c[0]
    if op == 'sleep':
        time.sleep(float(c[1]))
    elif op == 'shot':
        p = 'out/%s.ppm' % c[1]
        q.cmd('screendump', filename=p)
        time.sleep(1)
        Image.open(p).save(p[:-4] + '.png')
        print('saved', c[1])
    elif op == 'tap':
        x, y = float(c[1]), float(c[2])
        q.events(pos(x, y)); time.sleep(0.5)
        q.events(btn('left', True)); time.sleep(0.6)
        q.events(btn('left', False))
    elif op == 'drag':
        x1, y1, x2, y2, n = map(float, c[1:6])
        q.events(pos(x1, y1)); time.sleep(0.5)
        q.events(btn('left', True)); time.sleep(0.6)
        for k in range(1, int(n) + 1):
            q.events(pos(x1 + (x2 - x1) * k / n, y1 + (y2 - y1) * k / n))
            time.sleep(0.5)
        time.sleep(0.6)
        q.events(btn('left', False))
    elif op == 'home':
        q.events(btn('right', True)); time.sleep(0.6)
        q.events(btn('right', False))
    i += 1
