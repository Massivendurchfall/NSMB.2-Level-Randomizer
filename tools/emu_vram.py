"""Reads the state of the game's two VRAM allocators over the GDB stub of the self test emulator.

    python emu_vram.py [label]

The graphics memory manager singleton sits at [0x5EEC60] (EUR 1.0): +0x0C is the allocator of
VRAM A, +0x10 the one of VRAM B. An allocator keeps its used blocks in a list at +0x40 and its
free blocks in a list at +0x4C; a block has next at +0x08, size at +0x0C, address at +0x14.
"""
import struct
import sys

import emu_gdb

SINGLETON = 0x005EEC60


def u32(rsp, addr):
    raw = rsp.mem(addr, 4)
    return struct.unpack('<I', raw)[0] if len(raw) == 4 else 0


def walk(rsp, head):
    blocks = []
    node = head
    while node and len(blocks) < 5000:
        w = struct.unpack('<6I', rsp.mem(node, 24))
        blocks.append((w[5], w[3], w[4]))
        node = w[2]
    return blocks


def banks(rsp):
    mgr = u32(rsp, SINGLETON)
    out = []
    for name, off in (('VRAM A', 0x0C), ('VRAM B', 0x10)):
        alloc = u32(rsp, mgr + off)
        used = walk(rsp, u32(rsp, alloc + 0x40))
        free = walk(rsp, u32(rsp, alloc + 0x4C))
        out.append((name, alloc, used, free))
    return out


def main():
    label = sys.argv[1] if len(sys.argv) > 1 else ''
    detail = len(sys.argv) > 2
    rsp = emu_gdb.Rsp()
    rsp.send('?')
    for name, alloc, used, free in banks(rsp):
        sizes = [b[1] for b in free]
        print('%s%s used %8d in %4d blocks, free %8d in %3d blocks, largest %8d'
              % (label + ' ' if label else '', name, sum(b[1] for b in used), len(used),
                 sum(sizes), len(free), max(sizes) if sizes else 0))
        if detail:
            for addr, size, pad in sorted(free):
                print('     free %08x size %7d' % (addr, size))
    rsp.cont()


if __name__ == '__main__':
    main()
