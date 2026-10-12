"""Reader for NSMB2 level files (SARC and courseN.bin), format as documented by CoinKiller."""
import struct
from pathlib import Path

BLOCK_COUNT = 17
BLOCK_SPRITES = 7
BLOCK_SPRITES_USED = 8
SPRITE_SIZE = 24


def sarc_files(data):
    """Returns {name: (start, end)} with absolute offsets into data."""
    assert data[:4] == b'SARC', 'not a SARC'
    data_off = struct.unpack_from('<I', data, 0x0C)[0]
    assert data[0x14:0x18] == b'SFAT'
    node_count = struct.unpack_from('<H', data, 0x1A)[0]
    nodes = 0x20
    sfnt = nodes + node_count * 0x10
    assert data[sfnt:sfnt + 4] == b'SFNT'
    names = sfnt + 8
    out = {}
    for i in range(node_count):
        _hash, attr, start, end = struct.unpack_from('<4I', data, nodes + i * 0x10)
        name_off = names + (attr & 0xFFFFFF) * 4
        name = data[name_off:data.index(b'\0', name_off)].decode()
        out[name] = (data_off + start, data_off + end)
    return out


def course_blocks(course):
    """Returns the 17 (offset, size) pairs of the header."""
    return [struct.unpack_from('<II', course, i * 8) for i in range(BLOCK_COUNT)]


def course_sprites(course):
    """Returns the sprites as a list of dicts, offsets relative to the course start."""
    off, size = course_blocks(course)[BLOCK_SPRITES]
    out = []
    pos = off
    while pos + 2 <= off + size:
        sid = struct.unpack_from('<H', course, pos)[0]
        if sid == 0xFFFF:
            break
        x, y = struct.unpack_from('<HH', course, pos + 2)
        out.append({'offset': pos, 'id': sid, 'x': x, 'y': y,
                    'raw': bytes(course[pos:pos + SPRITE_SIZE])})
        pos += SPRITE_SIZE
    return out


def course_sprites_used(course):
    off, size = course_blocks(course)[BLOCK_SPRITES_USED]
    return [struct.unpack_from('<HH', course, off + i * 4) for i in range(size // 4)]


def iter_courses(course_dir):
    """Yields (level name, course name, bytes) for every courseN.bin of every level."""
    for path in sorted(Path(course_dir).glob('*.sarc')):
        data = path.read_bytes()
        for name, (start, end) in sorted(sarc_files(data).items()):
            if name.startswith('course/course') and name.endswith('.bin') and '_bgdat' not in name:
                yield path.stem, name, data[start:end]
