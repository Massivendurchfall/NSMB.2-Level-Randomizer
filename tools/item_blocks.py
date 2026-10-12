"""Lists the objects of the standard tileset (J_Kihon) that are item-carrying blocks."""
import struct
import sys

import nsmb2

d = open(sys.argv[1], 'rb').read()
files = nsmb2.sarc_files(d)


def get(name):
    a, b = files[name]
    return d[a:b]


chk = get('BG_chk/d_bgchk_J_Kihon.bin')
hd = get('BG_unt/J_Kihon_hd.bin')
unt = get('BG_unt/J_Kihon.bin')
print('tiles', len(chk) // 8, 'objects', len(hd) // 6)
for obj in range(len(hd) // 6):
    off, w, h, rnd = struct.unpack_from('<HBBH', hd, obj * 6)
    if w != 1 or h != 1:
        continue
    flags, lo, hi = unt[off], unt[off + 1], unt[off + 2]
    tile = lo | ((hi & 3) << 8)
    beh = chk[tile * 8:tile * 8 + 8]
    if beh[0] in (6, 7, 10) or (len(sys.argv) > 2 and beh[0]):
        print(f'obj {obj:3} tile {tile:3} flags {flags:02x} hi {hi:02x} behavior {beh.hex()}')
