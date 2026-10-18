#!/usr/bin/env python3
"""Makes some of the assets of Arena (the files next to this script), with nothing but the standard library:

    crate.obj    a crate (with crate.mtl and crate.png)
    floor.png, wall.png, platform.png    the textures of the level (the game makes the meshes)

They are made here, not drawn: the licence of every file is the one of this repository. Run it again to remake
them (the result is always the same):  python3 make_assets.py

robot.glb is not made here: it is the robot of the 3D platformer demo of Godot, see ROBOT.md.
"""
import json
import math
import os
import struct
import zlib

HERE = os.path.dirname(os.path.abspath(__file__))


# ----------------------------------------------------------------------------------------------------------
# PNG
# ----------------------------------------------------------------------------------------------------------

def write_png(name, width, height, pixel):
    """pixel(x, y) -> (r, g, b)"""
    raw = bytearray()
    for y in range(height):
        raw.append(0)
        for x in range(width):
            raw.extend(pixel(x, y))

    def chunk(kind, data):
        body = kind + data
        return struct.pack('>I', len(data)) + body + struct.pack('>I', zlib.crc32(body) & 0xFFFFFFFF)

    png = (b'\x89PNG\r\n\x1a\n' + chunk(b'IHDR', struct.pack('>IIBBBBB', width, height, 8, 2, 0, 0, 0))
           + chunk(b'IDAT', zlib.compress(bytes(raw), 9)) + chunk(b'IEND', b''))
    with open(os.path.join(HERE, name), 'wb') as out:
        out.write(png)


def noise(x, y, seed):
    """A value between 0 and 1 that is the same for the same arguments"""
    n = (x * 374761393 + y * 668265263 + seed * 2147483647) & 0xFFFFFFFF
    n = ((n ^ (n >> 13)) * 1274126177) & 0xFFFFFFFF
    return ((n ^ (n >> 16)) & 0xFFFF) / 65535.0


def shade(color, k):
    return tuple(max(0, min(255, int(c * k))) for c in color)


def make_textures():
    # A floor of tiles: 8 x 8 tiles of 16 pixels, with a lighter edge and a little grain
    def floor(x, y):
        edge = x % 16 == 0 or y % 16 == 0
        tile = 0.85 + 0.15 * noise(x // 16, y // 16, 1)
        return shade((92, 98, 106), (1.35 if edge else tile) * (0.95 + 0.1 * noise(x, y, 2)))

    # Bricks: rows of 16 pixels, shifted by half a brick every other row, with mortar between
    def wall(x, y):
        row = y // 16
        shift = 16 if row % 2 else 0
        brick = (x + shift) // 32
        mortar = y % 16 < 2 or (x + shift) % 32 < 2
        base = (150, 84, 66) if noise(brick, row, 3) > 0.3 else (132, 72, 58)
        return shade((176, 170, 160) if mortar else base, 0.9 + 0.2 * noise(x, y, 4))

    # Concrete, with lines like the joints of poured slabs
    def platform(x, y):
        joint = x % 64 == 0 or y % 64 == 0
        return shade((150, 152, 150), (0.7 if joint else 1.0) * (0.88 + 0.24 * noise(x, y, 5)))

    # Planks of 16 pixels, a dark frame, a cross of boards
    def crate(x, y):
        frame = x < 6 or y < 6 or x >= 58 or y >= 58
        cross = abs(x - y) < 4 or abs(x + y - 63) < 4
        plank = y % 16 < 1
        wood = (186, 132, 66) if frame or cross else (160, 108, 52)
        return shade(wood, (0.55 if plank else 1.0) * (0.85 + 0.3 * noise(x, y // 4, 6)))

    write_png('floor.png', 128, 128, floor)
    write_png('wall.png', 128, 128, wall)
    write_png('platform.png', 128, 128, platform)
    write_png('crate.png', 64, 64, crate)


# ----------------------------------------------------------------------------------------------------------
# A box, as the vertices of six faces (positions, normals, uvs) and indices
# ----------------------------------------------------------------------------------------------------------

FACES = [  # normal, u axis, v axis (u x v = normal)
    ((1, 0, 0), (0, 0, -1), (0, 1, 0)),
    ((-1, 0, 0), (0, 0, 1), (0, 1, 0)),
    ((0, 1, 0), (1, 0, 0), (0, 0, -1)),
    ((0, -1, 0), (1, 0, 0), (0, 0, 1)),
    ((0, 0, 1), (1, 0, 0), (0, 1, 0)),
    ((0, 0, -1), (-1, 0, 0), (0, 1, 0)),
]


def box(center, size):
    positions, normals, uvs, indices = [], [], [], []
    half = [s / 2 for s in size]
    for n, u, v in FACES:
        first = len(positions)
        c = [center[i] + n[i] * half[i] for i in range(3)]
        for su, sv in ((-1, -1), (1, -1), (1, 1), (-1, 1)):
            positions.append(tuple(c[i] + u[i] * half[i] * su + v[i] * half[i] * sv for i in range(3)))
            normals.append(n)
            uvs.append(((su + 1) / 2, 1 - (sv + 1) / 2))
        indices.extend([first, first + 1, first + 2, first, first + 2, first + 3])
    return positions, normals, uvs, indices


# ----------------------------------------------------------------------------------------------------------
# The crate, as an OBJ
# ----------------------------------------------------------------------------------------------------------

def make_crate():
    positions, normals, uvs, indices = box((0, 0, 0), (1, 1, 1))
    lines = ['# A crate of 1 m (made by make_assets.py)', 'mtllib crate.mtl', 'o crate']
    lines += ['v %g %g %g' % p for p in positions]
    lines += ['vt %g %g' % t for t in uvs]
    lines += ['vn %g %g %g' % n for n in normals]
    lines.append('usemtl crate')
    for i in range(0, len(indices), 3):
        a, b, c = (j + 1 for j in indices[i:i + 3])
        lines.append('f %d/%d/%d %d/%d/%d %d/%d/%d' % (a, a, a, b, b, b, c, c, c))
    with open(os.path.join(HERE, 'crate.obj'), 'w') as out:
        out.write('\n'.join(lines) + '\n')
    with open(os.path.join(HERE, 'crate.mtl'), 'w') as out:
        out.write('newmtl crate\nKd 1 1 1\nKs 0.05 0.05 0.05\nNs 8\nillum 2\nmap_Kd crate.png\n')


if __name__ == '__main__':
    make_textures()
    make_crate()
    print('assets written to', HERE)
