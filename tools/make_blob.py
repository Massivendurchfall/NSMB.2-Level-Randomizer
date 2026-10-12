"""Packs every course file into one file for the host test: u32 size, then the data, per course."""
import struct
import sys

import nsmb2

with open(sys.argv[2], 'wb') as out:
    n = 0
    for _level, _name, course in nsmb2.iter_courses(sys.argv[1]):
        out.write(struct.pack('<I', len(course)))
        out.write(course)
        n += 1
print(n, 'courses')
