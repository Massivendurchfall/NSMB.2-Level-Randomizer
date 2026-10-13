"""Reads the free space of the game's heaps over the GDB stub of the self test emulator.

    python emu_heaps.py [label]

The game keeps its sead::ExpHeap pointers in a table at 0x5EEC78 (EUR 1.0). A heap object holds
its name at +0x14, its size at +0x1C and its free list at +0x74 (previous, next, count); a free
block stores its size at +0x08. Prints one line per heap: size, free bytes, largest free block.
"""
import struct
import sys

import emu_gdb

TABLE = 0x005EEC78
SLOTS = 22


def u32(rsp, addr):
    raw = rsp.mem(addr, 4)
    return struct.unpack('<I', raw)[0] if len(raw) == 4 else 0


def cstr(rsp, addr):
    raw = rsp.mem(addr, 40)
    return raw.split(b'\0')[0].decode('latin1')


def heaps(rsp):
    out = []
    for slot in range(SLOTS):
        heap = u32(rsp, TABLE + slot * 4)
        if heap < 0x08000000 or heap >= 0x30000000:
            continue
        head = struct.unpack('<8I', rsp.mem(heap, 32))
        if head[0] != 0x005D1C68:
            continue
        name = cstr(rsp, head[5])
        size = head[7]
        _prev, node, count = struct.unpack('<3I', rsp.mem(heap + 0x74, 12))
        free = largest = 0
        for _ in range(min(count, 4000)):
            block = struct.unpack('<4I', rsp.mem(node, 16))
            free += block[2]
            largest = max(largest, block[2])
            node = block[1]
        used = u32(rsp, heap + 0x8C)
        out.append((name, size, free, largest, count, used))
    return out


def main():
    label = sys.argv[1] if len(sys.argv) > 1 else ''
    rsp = emu_gdb.Rsp()
    rsp.send('?')
    for name, size, free, largest, count, used in heaps(rsp):
        print('%s%-18s size %8d  free %8d  largest %8d  free blocks %3d  used blocks %4d'
              % (label + ' ' if label else '', name, size, free, largest, count, used))
    rsp.cont()


if __name__ == '__main__':
    main()
