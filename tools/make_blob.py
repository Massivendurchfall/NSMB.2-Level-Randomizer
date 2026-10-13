"""Packs every course file into one file for the host test.

Per course: world, level number and area as one byte each, a zero byte, u32 size, then the data.
The courses of a level follow each other, area 1 first.
"""
import struct
import sys

import nsmb2

with open(sys.argv[2], 'wb') as out:
    n = 0
    for level, name, course in nsmb2.iter_courses(sys.argv[1]):
        world, number = (int(x) for x in level.split('-'))
        area = int(name[len('course/course')])
        out.write(struct.pack('<BBBBI', world, number, area, 0, len(course)))
        out.write(course)
        n += 1
print(n, 'courses')
