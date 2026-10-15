"""Send commands to the QEMU monitor socket. Usage: qmon.py CMD [CMD...]
'shot NAME' saves out/NAME.png; anything else is passed through; 'sleep N' waits."""
import socket, sys, time
from PIL import Image
import os
sock = os.environ.get('QMON', 'out/qemu-mon.sock')
s = socket.socket(socket.AF_UNIX); s.connect(sock); time.sleep(0.3); s.recv(65536)
for c in sys.argv[1:]:
    if c.startswith('sleep '):
        time.sleep(float(c.split()[1])); continue
    if c.startswith('shot '):
        name = c.split()[1]; p = 'out/%s.ppm' % name
        s.send(('screendump %s\n' % p).encode()); time.sleep(2)
        Image.open(p).save(p[:-4] + '.png'); print('saved', name); continue
    s.send((c + '\n').encode()); time.sleep(0.2)
s.close()
