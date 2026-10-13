"""Talks to the GDB stub of the self test emulator (Azahar, port 24689).

    python emu_gdb.py watch <plugin log> <out file>

connects (the emulator waits for that before it boots the game), lets the game run and watches
the frame counter in the plugin log. When it has stood still for a while, the game hangs: the
script halts it and writes the registers and a raw stack scan of every thread to the out file.
"""
import os
import re
import socket
import struct
import sys
import time

PORT = int(os.environ.get('EMU_GDB_PORT', '24689'))
CODE_LO, CODE_HI = 0x00100000, 0x00100000 + 0x469484


class Rsp:
    def __init__(self):
        deadline = time.time() + 120
        while True:
            try:
                self.s = socket.create_connection(('127.0.0.1', PORT), timeout=2)
                break
            except OSError:
                if time.time() > deadline:
                    raise
                time.sleep(0.5)
        self.s.settimeout(10)
        self.buf = b''

    def _read(self, n=4096):
        data = self.s.recv(n)
        if not data:
            raise ConnectionError('stub closed the connection')
        self.buf += data

    def send(self, payload, reply=True):
        if isinstance(payload, str):
            payload = payload.encode()
        self.s.sendall(b'$' + payload + b'#' + (b'%02x' % (sum(payload) & 0xFF)))
        return self.recv() if reply else None

    def recv(self, timeout=10):
        self.s.settimeout(timeout)
        while True:
            start = self.buf.find(b'$')
            end = self.buf.find(b'#', start + 1) if start >= 0 else -1
            if start >= 0 and end >= 0 and len(self.buf) >= end + 3:
                body = self.buf[start + 1:end]
                self.buf = self.buf[end + 3:]
                self.s.sendall(b'+')
                return body.decode('latin1')
            self._read()

    def cont(self):
        self.send('c', reply=False)

    def interrupt(self):
        self.s.sendall(b'\x03')
        return self.recv(timeout=20)

    def threads(self):
        out = []
        r = self.send('qfThreadInfo')
        while r.startswith('m'):
            out += [int(t, 16) for t in r[1:].split(',') if t]
            r = self.send('qsThreadInfo')
        return out

    def regs(self, tid):
        self.send('Hg%x' % tid)
        raw = self.send('g')
        vals = [struct.unpack('<I', bytes.fromhex(raw[i * 8:i * 8 + 8]))[0] for i in range(16)
                if len(raw) >= i * 8 + 8 and 'x' not in raw[i * 8:i * 8 + 8]]
        return vals

    def mem(self, addr, size):
        out = b''
        while size:
            n = min(size, 0x400)
            r = self.send('m%x,%x' % (addr, n))
            if r.startswith('E') or not r:
                break
            out += bytes.fromhex(r)
            addr += n
            size -= n
        return out


def last_frame(path):
    try:
        text = open(path, 'r', errors='replace').read()
    except OSError:
        return 0
    found = re.findall(r'^Frame (\d+)', text, re.M)
    return int(found[-1]) if found else 0


def dump(rsp, out):
    with open(out, 'w') as f:
        for tid in rsp.threads():
            r = rsp.regs(tid)
            if len(r) < 16:
                f.write('thread %d: no registers\n' % tid)
                continue
            f.write('thread %d: pc %08x lr %08x sp %08x\n  r0-r12 %s\n' % (
                tid, r[15], r[14], r[13], ' '.join('%08x' % v for v in r[:13])))
            stack = rsp.mem(r[13], 0x300)
            words = struct.unpack('<%dI' % (len(stack) // 4), stack[:len(stack) // 4 * 4])
            code = ['%08x' % w for w in words if CODE_LO <= w < CODE_HI and w % 2 == 0]
            f.write('  stack code words: %s\n' % ' '.join(code[:40]))
        f.write('done\n')


def main():
    log, out = sys.argv[2], sys.argv[3]
    rsp = Rsp()
    print('connected', flush=True)
    rsp.cont()
    seen, since = 0, time.time()
    while True:
        time.sleep(2)
        frame = last_frame(log)
        if frame != seen:
            seen, since = frame, time.time()
        elif seen and time.time() - since > 25:
            print('frame counter stands at %d, halting' % seen, flush=True)
            print('stop reply', rsp.interrupt(), flush=True)
            dump(rsp, out)
            print('dump written', flush=True)
            return


if __name__ == '__main__':
    main()
