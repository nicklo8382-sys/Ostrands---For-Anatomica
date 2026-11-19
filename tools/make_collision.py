import math
from pathlib import Path

from make_strand import (ALPHA_BLEND, SLSF1_OWN_EMIT, SLSF1_SKINNED, SLSF1_ZBUFFER_TEST,
                         SLSF2_DOUBLE_SIDED, TEXTURE_DIR, build_skinned_nif,
                         dds_bgra, f32, i32, u32)


BONES = 16
SIDES = 32
ALPHA = 0.45


def geometry():
    verts = []
    for bone in range(BONES):
        for side in range(SIDES):
            angle = math.tau * side / SIDES
            c, s = math.cos(angle), math.sin(angle)
            verts.append(((c, float(bone), s), (side / SIDES, bone / (BONES - 1)),
                          (c, 0.0, s), (-s, 0.0, c), (0.0, 1.0, 0.0), bone))
    tris = []
    for bone in range(BONES - 1):
        for side in range(SIDES):
            a = bone * SIDES + side
            b = bone * SIDES + (side + 1) % SIDES
            tris.extend(((a, a + SIDES, b), (b, a + SIDES, b + SIDES)))
    return verts, tris


def shader(texset):
    flags1 = SLSF1_SKINNED | SLSF1_OWN_EMIT | SLSF1_ZBUFFER_TEST
    flags2 = SLSF2_DOUBLE_SIDED
    out = u32(0) + i32(-1) + u32(0) + i32(-1) + u32(flags1) + u32(flags2)
    out += f32(0.0, 0.0) + f32(1.0, 1.0) + i32(texset)
    out += f32(0.0, 0.8, 1.0) + f32(1.0)
    out += u32(3)
    out += f32(ALPHA) + f32(0.0) + f32(1.0)
    out += f32(0.0, 0.0, 0.0) + f32(0.0)
    out += f32(0.0) + f32(0.0)
    return out


def main():
    data = Path(__file__).resolve().parent.parent / 'data'
    mesh_dir = data / 'meshes/OStrands'
    tex_dir = data / 'textures/OStrands'
    mesh_dir.mkdir(parents=True, exist_ok=True)
    tex_dir.mkdir(parents=True, exist_ok=True)
    verts, tris = geometry()
    length = BONES - 1
    nif, vertices, triangles = build_skinned_nif(
        'CollisionRoot', [f'Rope{i:02}' for i in range(BONES)],
        [(0.0, float(i), 0.0) for i in range(BONES)], 'RopeShape', verts, tris,
        (0.0, length / 2, 0.0, math.hypot(length / 2, 1.0)), 1.0,
        shader, [TEXTURE_DIR + 'collision.dds', TEXTURE_DIR + 'collision_n.dds'], ALPHA_BLEND)
    (mesh_dir / 'collision.nif').write_bytes(nif)
    (tex_dir / 'collision.dds').write_bytes(dds_bgra(4, 4, lambda x, y: (0, 204, 255, 255)))
    (tex_dir / 'collision_n.dds').write_bytes(
        dds_bgra(4, 4, lambda x, y: (128, 128, 255, 0), normal=True))
    print(f'collision.nif: {vertices} vertices, {triangles} triangles, {len(nif)} bytes')


if __name__ == '__main__':
    main()
