"""Reads a Luma3DS crash dump (luma/dumps/arm11/*.dmp) and prints what matters for this plugin.

Usage: python crash_dump.py <dump.dmp> [more dumps ...]
"""
import struct
import sys

TYPES = {0: 'FIQ', 1: 'undefined instruction', 2: 'prefetch abort', 3: 'data abort'}
REGS = ['r0', 'r1', 'r2', 'r3', 'r4', 'r5', 'r6', 'r7', 'r8', 'r9', 'r10', 'r11', 'r12',
        'sp', 'lr', 'pc', 'cpsr', 'dfsr', 'ifsr', 'far', 'fpexc', 'fpinst', 'fpinst2']
GAME_TEXT = (0x00100000, 0x00100000 + 0x469484)


def where(addr):
    if GAME_TEXT[0] <= addr < GAME_TEXT[1]:
        return 'game code'
    if 0x07000000 <= addr < 0x07800000:
        return 'plugin'
    if 0x00569000 <= addr < 0x00700000:
        return 'game data'
    if 0x08000000 <= addr < 0x10000000:
        return 'heap'
    if 0x14000000 <= addr < 0x20000000 or 0x30000000 <= addr < 0x40000000:
        return 'linear heap'
    if addr < 0x1000:
        return 'null'
    return ''


def main():
    for path in sys.argv[1:]:
        d = open(path, 'rb').read()
        magic1, magic2, vmaj, vmin, processor, core, kind, total, reg_size, code_size, stack_size, extra_size = \
            struct.unpack_from('<IIHHHHIIIIII', d, 0)
        print(f'== {path}')
        if (magic1, magic2) != (0xDEADC0DE, 0xDEADCAFE):
            print('   not a Luma crash dump')
            continue
        regs = struct.unpack_from(f'<{reg_size // 4}I', d, 40)
        named = dict(zip(REGS, regs))
        code = d[40 + reg_size:40 + reg_size + code_size]
        stack = d[40 + reg_size + code_size:40 + reg_size + code_size + stack_size]
        extra = d[40 + reg_size + code_size + stack_size:]
        name = extra[:8].rstrip(b'\0').decode(errors='replace')
        title = struct.unpack_from('<Q', extra, 8)[0] if len(extra) >= 16 else 0
        print(f'   {TYPES.get(kind, kind)} on ARM{processor} core {core}, process {name!r}, title {title:016X}')
        for i in range(0, len(REGS[:16]), 4):
            print('   ' + '  '.join(f'{r:>4} {named[r]:08X}' for r in REGS[i:i + 4] if r in named))
        print(f"   cpsr {named.get('cpsr', 0):08X}  far {named.get('far', 0):08X}  dfsr {named.get('dfsr', 0):08X}")
        print(f"   pc is in: {where(named['pc']) or '?'}, lr is in: {where(named['lr']) or '?'}, "
              f"faulting address is: {where(named.get('far', 0)) or '?'}")
        words = struct.unpack_from(f'<{len(code) // 4}I', code, 0) if code else ()
        print('   code up to pc: ' + ' '.join(f'{w:08X}' for w in words[-6:]))
        hits = []
        for i in range(0, len(stack) - 3, 4):
            v = struct.unpack_from('<I', stack, i)[0]
            if where(v) in ('game code', 'plugin'):
                hits.append(f'{v:08X}({where(v)[0]})')
        print('   code addresses on the stack: ' + ' '.join(hits[:28]))


if __name__ == '__main__':
    main()
