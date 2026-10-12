"""Decrypts a CIA with the boot9.bin of your own console and unpacks it (needs pyctr).

Usage: python decrypt_extract.py <file.cia> <boot9.bin> <output folder>

Writes code.bin (the decompressed .code section), exheader.bin and the RomFS tree.
"""
import sys
from pathlib import Path

from pyctr.crypto import CryptoEngine
from pyctr.type.cia import CIAReader, CIASection
from pyctr.type.ncch import NCCHSection
from pyctr.type.romfs import RomFSDirectoryEntry


def dump_romfs(romfs, dest):
    count = 0

    def walk(path, target):
        nonlocal count
        target.mkdir(parents=True, exist_ok=True)
        for name in romfs.get_info_from_path(path).contents:
            child = path.rstrip('/') + '/' + name
            if isinstance(romfs.get_info_from_path(child), RomFSDirectoryEntry):
                walk(child, target / name)
            else:
                with romfs.open(child) as src:
                    (target / name).write_bytes(src.read())
                count += 1

    walk('/', dest)
    return count


def main():
    cia_path, boot9, out_dir = sys.argv[1], sys.argv[2], Path(sys.argv[3])
    out_dir.mkdir(parents=True, exist_ok=True)
    crypto = CryptoEngine(boot9=boot9)
    with CIAReader(cia_path, crypto=crypto) as cia:
        print(f'Title ID {cia.tmd.title_id}, version {cia.tmd.title_version}')
        app = cia.contents[CIASection.Application]
        print(f'Product code {app.product_code}, program ID {app.program_id}')

        with app.open_raw_section(NCCHSection.ExtendedHeader) as f:
            (out_dir / 'exheader.bin').write_bytes(f.read())

        app.exefs.decompress_code()
        names = list(app.exefs.entries)
        code_name = '.code-decompressed' if '.code-decompressed' in names else '.code'
        with app.exefs.open(code_name) as f:
            code = f.read()
        (out_dir / 'code.bin').write_bytes(code)
        print(f'ExeFS {names}, code.bin from {code_name}: {len(code)} bytes')

        n = dump_romfs(app.romfs, out_dir / 'romfs')
        print(f'RomFS: {n} files')


if __name__ == '__main__':
    main()
