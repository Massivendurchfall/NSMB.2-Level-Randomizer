"""One line with the free memory that matters: the actor heap, the model heaps and both VRAM banks.

    python emu_stat.py [label]

Read over the GDB stub of the self test emulator, see emu_heaps.py and emu_vram.py.
"""
import sys

import emu_gdb
import emu_heaps
import emu_vram

WANT = ('ActorHeap', 'ModelResourceHeap', 'ModelSetupHeap', 'AreaHeap')


def main():
    label = sys.argv[1] if len(sys.argv) > 1 else ''
    rsp = emu_gdb.Rsp()
    rsp.send('?')
    parts = []
    for name, size, free, largest, count, used in emu_heaps.heaps(rsp):
        if name in WANT:
            parts.append('%s %d' % (name, free))
    for name, alloc, used, free in emu_vram.banks(rsp):
        sizes = [b[1] for b in free]
        parts.append('%s %d (largest %d)' % (name.replace(' ', ''), sum(sizes), max(sizes) if sizes else 0))
    print('%s | %s' % (label, ' | '.join(parts)))
    rsp.cont()


if __name__ == '__main__':
    main()
