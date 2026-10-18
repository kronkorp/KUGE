#!/usr/bin/env python3
"""Makes the assets of Arena (the files next to this script), with nothing but the standard library:

    player.glb   a character: a skeleton of nodes with a box for each part, and the clips
                 idle, run, jump, shoot and death. Its shirt is the material "team" (pure cyan, unlit):
                 the game paints it with the colour of each player.
    crate.obj    a crate (with crate.mtl and crate.png)
    floor.png, wall.png, platform.png    the textures of the level (the game makes the meshes)

They are made here, not drawn: the licence of every file is the one of this repository. Run it again to remake
them (the result is always the same):  python3 make_assets.py
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


# ----------------------------------------------------------------------------------------------------------
# The character, as a glTF
# ----------------------------------------------------------------------------------------------------------

def quat_x(angle):
    return (math.sin(angle / 2), 0.0, 0.0, math.cos(angle / 2))


def quat_mul(a, b):
    ax, ay, az, aw = a
    bx, by, bz, bw = b
    return (aw * bx + ax * bw + ay * bz - az * by, aw * by - ax * bz + ay * bw + az * bx,
            aw * bz + ax * by - ay * bx + az * bw, aw * bw - ax * bx - ay * by - az * bz)


class Gltf:
    def __init__(self):
        self.buffer = bytearray()
        self.views, self.accessors, self.meshes, self.materials, self.nodes = [], [], [], [], []
        self.animations = []
        self.material_index = {}

    def add_accessor(self, values, kind, component, count_type, minmax=False):
        data = b''.join(struct.pack('<' + component * len(v), *v) if isinstance(v, tuple) else struct.pack('<' + component, v)
                        for v in values)
        while len(self.buffer) % 4:
            self.buffer.append(0)
        self.views.append({'buffer': 0, 'byteOffset': len(self.buffer), 'byteLength': len(data)})
        self.buffer.extend(data)
        accessor = {'bufferView': len(self.views) - 1, 'componentType': kind, 'count': len(values), 'type': count_type}
        if minmax:
            columns = list(zip(*values)) if isinstance(values[0], tuple) else [values]
            accessor['min'] = [min(c) for c in columns]
            accessor['max'] = [max(c) for c in columns]
        self.accessors.append(accessor)
        return len(self.accessors) - 1

    def material(self, name, color, unlit=False):
        if name not in self.material_index:
            material = {'name': name, 'pbrMetallicRoughness': {'baseColorFactor': list(color) + [1.0], 'metallicFactor': 0.0,
                                                                  'roughnessFactor': 0.8}}
            if unlit:
                material['extensions'] = {'KHR_materials_unlit': {}}
            self.materials.append(material)
            self.material_index[name] = len(self.materials) - 1
        return self.material_index[name]

    def mesh(self, center, size, material):
        positions, normals, _, indices = box(center, size)
        primitive = {'attributes': {'POSITION': self.add_accessor(positions, 5126, 'f', 'VEC3', True),
                                    'NORMAL': self.add_accessor(normals, 5126, 'f', 'VEC3')},
                     'indices': self.add_accessor(indices, 5123, 'H', 'SCALAR'), 'material': material}
        self.meshes.append({'primitives': [primitive]})
        return len(self.meshes) - 1

    def node(self, name, translation, meshes=(), parent=None):
        node = {'name': name, 'translation': list(translation)}
        index = len(self.nodes)
        self.nodes.append(node)
        if meshes:
            # (one mesh per node in glTF: further parts are children with no move of their own)
            node['mesh'] = meshes[0]
            for i, extra in enumerate(meshes[1:]):
                child = len(self.nodes)
                self.nodes.append({'name': '%s_%d' % (name, i + 1), 'mesh': extra})
                node.setdefault('children', []).append(child)
        if parent is not None:
            self.nodes[parent].setdefault('children', []).append(index)
        return index

    def clip(self, name, duration, tracks):
        """tracks: {node: {'rotation' or 'translation': [(time, value), ...]}}"""
        samplers, channels = [], []
        for node, paths in tracks.items():
            for path, keys in paths.items():
                times = [t for t, _ in keys]
                values = [v for _, v in keys]
                samplers.append({'input': self.add_accessor(times, 5126, 'f', 'SCALAR', True),
                                 'output': self.add_accessor(values, 5126, 'f',
                                                             'VEC4' if path == 'rotation' else 'VEC3'),
                                 'interpolation': 'LINEAR'})
                channels.append({'sampler': len(samplers) - 1, 'target': {'node': node, 'path': path}})
        self.animations.append({'name': name, 'samplers': samplers, 'channels': channels})

    def write(self, name):
        document = {
            'asset': {'version': '2.0', 'generator': 'make_assets.py'},
            'extensionsUsed': ['KHR_materials_unlit'],
            'scene': 0, 'scenes': [{'nodes': [0]}], 'nodes': self.nodes, 'meshes': self.meshes,
            'materials': self.materials, 'accessors': self.accessors, 'bufferViews': self.views,
            'buffers': [{'byteLength': len(self.buffer)}], 'animations': self.animations,
        }
        text = json.dumps(document, separators=(',', ':')).encode()
        text += b' ' * (-len(text) % 4)
        binary = bytes(self.buffer) + b'\0' * (-len(self.buffer) % 4)
        total = 12 + 8 + len(text) + 8 + len(binary)
        with open(os.path.join(HERE, name), 'wb') as out:
            out.write(struct.pack('<III', 0x46546C67, 2, total))
            out.write(struct.pack('<II', len(text), 0x4E4F534A) + text)
            out.write(struct.pack('<II', len(binary), 0x004E4942) + binary)


def make_player():
    g = Gltf()
    skin = g.material('skin', (0.62, 0.38, 0.26))
    team = g.material('team', (0.0, 1.0, 1.0), unlit=True)    # painted by the game
    pants = g.material('pants', (0.04, 0.06, 0.16))
    metal = g.material('metal', (0.07, 0.07, 0.08))
    visor = g.material('visor', (0.0, 0.0, 0.0), unlit=True)

    # The model is centred on the middle of the capsule of the physics: the feet are 0.9 m under the origin
    root = g.node('root', (0, 0, 0))
    hips = g.node('hips', (0, -0.08, 0), [g.mesh((0, 0.02, 0), (0.42, 0.18, 0.26), pants)], root)
    torso = g.node('torso', (0, 0.1, 0), [g.mesh((0, 0.27, 0), (0.54, 0.56, 0.3), team)], hips)
    head = g.node('head', (0, 0.6, 0), [g.mesh((0, 0.15, 0), (0.3, 0.3, 0.3), skin),
                                       g.mesh((0, 0.17, -0.155), (0.24, 0.1, 0.02), visor)], torso)
    arm_l = g.node('arm_l', (-0.36, 0.5, 0), [g.mesh((0, -0.18, 0), (0.13, 0.38, 0.13), team)], torso)
    fore_l = g.node('forearm_l', (0, -0.38, 0), [g.mesh((0, -0.16, 0), (0.11, 0.32, 0.11), skin)], arm_l)
    arm_r = g.node('arm_r', (0.36, 0.5, 0), [g.mesh((0, -0.18, 0), (0.13, 0.38, 0.13), team)], torso)
    fore_r = g.node('forearm_r', (0, -0.38, 0), [g.mesh((0, -0.16, 0), (0.11, 0.32, 0.11), skin),
                                                 g.mesh((0, -0.3, -0.18), (0.08, 0.12, 0.5), metal)], arm_r)
    thigh_l = g.node('thigh_l', (-0.15, -0.05, 0), [g.mesh((0, -0.2, 0), (0.18, 0.42, 0.18), pants)], hips)
    shin_l = g.node('shin_l', (0, -0.42, 0), [g.mesh((0, -0.2, 0), (0.15, 0.4, 0.15), pants)], thigh_l)
    thigh_r = g.node('thigh_r', (0.15, -0.05, 0), [g.mesh((0, -0.2, 0), (0.18, 0.42, 0.18), pants)], hips)
    shin_r = g.node('shin_r', (0, -0.42, 0), [g.mesh((0, -0.2, 0), (0.15, 0.4, 0.15), pants)], thigh_r)

    def rot(*keys):
        return [(t, quat_x(a)) for t, a in keys]

    def hold(angle, duration):
        return rot((0.0, angle), (duration, angle))

    # idle: breathing, the gun held ahead
    g.clip('idle', 2.0, {
        torso: {'rotation': rot((0, 0.02), (1, -0.02), (2, 0.02))},
        arm_r: {'rotation': rot((0, 1.35), (1, 1.4), (2, 1.35))},
        arm_l: {'rotation': rot((0, 0.1), (1, 0.05), (2, 0.1))},
        fore_r: {'rotation': hold(0.1, 2.0)},
        hips: {'translation': [(0, (0, -0.08, 0)), (1, (0, -0.095, 0)), (2, (0, -0.08, 0))]},
    })
    # run: a stride in 0.6 s, the body leaning ahead, the gun held ahead
    s = 0.15
    g.clip('run', 0.6, {
        thigh_l: {'rotation': rot((0, 0.9), (s, 0.1), (2 * s, -0.8), (3 * s, 0.1), (4 * s, 0.9))},
        thigh_r: {'rotation': rot((0, -0.8), (s, 0.1), (2 * s, 0.9), (3 * s, 0.1), (4 * s, -0.8))},
        shin_l: {'rotation': rot((0, 0.1), (s, 0.9), (2 * s, 0.7), (3 * s, 0.1), (4 * s, 0.1))},
        shin_r: {'rotation': rot((0, 0.7), (s, 0.1), (2 * s, 0.1), (3 * s, 0.9), (4 * s, 0.7))},
        arm_l: {'rotation': rot((0, -0.9), (2 * s, 0.9), (4 * s, -0.9))},
        arm_r: {'rotation': rot((0, 1.3), (2 * s, 1.5), (4 * s, 1.3))},
        fore_l: {'rotation': hold(0.9, 0.6)},
        fore_r: {'rotation': hold(0.15, 0.6)},
        torso: {'rotation': hold(0.2, 0.6)},
        hips: {'translation': [(0, (0, -0.1, 0)), (s, (0, -0.06, 0)), (2 * s, (0, -0.1, 0)), (3 * s, (0, -0.06, 0)), (4 * s, (0, -0.1, 0))]},
    })
    # jump: crouch, then the legs tucked and the arms up (it holds the last pose while in the air)
    g.clip('jump', 0.8, {
        thigh_l: {'rotation': rot((0, 0.9), (0.25, 0.7), (0.8, 0.8))},
        thigh_r: {'rotation': rot((0, 0.9), (0.25, 0.2), (0.8, 0.3))},
        shin_l: {'rotation': rot((0, 1.1), (0.25, 0.9), (0.8, 1.0))},
        shin_r: {'rotation': rot((0, 1.1), (0.25, 0.4), (0.8, 0.5))},
        arm_l: {'rotation': rot((0, 0.3), (0.25, 2.6), (0.8, 2.5))},
        arm_r: {'rotation': rot((0, 1.3), (0.25, 2.3), (0.8, 2.2))},
        fore_l: {'rotation': hold(0.3, 0.8)},
        fore_r: {'rotation': hold(0.3, 0.8)},
        torso: {'rotation': rot((0, 0.3), (0.25, -0.1), (0.8, -0.05))},
        hips: {'translation': [(0, (0, -0.25, 0)), (0.25, (0, -0.05, 0)), (0.8, (0, -0.05, 0))]},
    })
    # shoot: the gun kicks up and comes back
    g.clip('shoot', 0.35, {
        arm_r: {'rotation': rot((0, 1.4), (0.06, 1.9), (0.35, 1.4))},
        fore_r: {'rotation': rot((0, 0.1), (0.06, 0.5), (0.35, 0.1))},
        torso: {'rotation': rot((0, 0.05), (0.06, -0.12), (0.35, 0.05))},
        arm_l: {'rotation': hold(0.4, 0.35)},
    })
    # death: falls on its back
    g.clip('death', 1.0, {
        root: {'rotation': rot((0, 0.0), (0.7, 1.5), (1.0, 1.55)),
               'translation': [(0, (0, 0, 0)), (0.7, (0, -0.6, 0.2)), (1.0, (0, -0.75, 0.25))]},
        arm_l: {'rotation': rot((0, 0.1), (1.0, 1.0))},
        arm_r: {'rotation': rot((0, 1.3), (1.0, -0.6))},
        thigh_l: {'rotation': rot((0, 0.0), (1.0, 0.3))},
        thigh_r: {'rotation': rot((0, 0.0), (1.0, -0.2))},
        head: {'rotation': rot((0, 0.0), (1.0, -0.3))},
    })
    g.write('player.glb')


if __name__ == '__main__':
    make_textures()
    make_crate()
    make_player()
    print('assets written to', HERE)
