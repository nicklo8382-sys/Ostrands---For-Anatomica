import os
import struct
import sys

from make_strand import dds_bgra

SIZE = 256


def rgb565(c):
    return (((c >> 11) & 31) * 255 // 31, ((c >> 5) & 63) * 255 // 63, (c & 31) * 255 // 31)


def decode_dxt5(data, width, height):
    alpha = [[0] * width for _ in range(height)]
    offset = 0
    for by in range((height + 3) // 4):
        for bx in range((width + 3) // 4):
            a0, a1 = data[offset], data[offset + 1]
            bits = int.from_bytes(data[offset + 2:offset + 8], 'little')
            if a0 > a1:
                table = [a0, a1] + [((7 - i) * a0 + i * a1) // 7 for i in range(1, 7)]
            else:
                table = [a0, a1] + [((5 - i) * a0 + i * a1) // 5 for i in range(1, 5)] + [0, 255]
            for i in range(16):
                x, y = bx * 4 + i % 4, by * 4 + i // 4
                if x < width and y < height:
                    alpha[y][x] = table[(bits >> (3 * i)) & 7]
            offset += 16
    return alpha


def decode_bgra(data, width, height, mask):
    shift = (mask & -mask).bit_length() - 1
    alpha = []
    for y in range(height):
        row = struct.unpack_from('<%dI' % width, data, y * width * 4)
        alpha.append([(p & mask) >> shift for p in row])
    return alpha


def read_alpha(path):
    d = open(path, 'rb').read()
    if d[:4] != b'DDS ':
        raise SystemExit('%s is not a DDS file' % path)
    height, width = struct.unpack('<II', d[12:20])
    flags, fourcc, bits = struct.unpack('<I4sI', d[80:92])
    alpha_mask = struct.unpack('<I', d[104:108])[0]
    if fourcc == b'DXT5':
        return width, height, decode_dxt5(d[128:], width, height)
    if flags & 0x40 and bits == 32 and alpha_mask:
        return width, height, decode_bgra(d[128:], width, height, alpha_mask)
    raise SystemExit('Unsupported DDS format; save the star as DXT5 (BC3) or uncompressed 32-bit BGRA with alpha.')


def resample(lines, size):
    source = len(lines[0])
    scale = source / size
    result = []
    for line in lines:
        out = []
        for o in range(size):
            start, end = o * scale, (o + 1) * scale
            total = 0.0
            i = int(start)
            while i < end and i < source:
                total += line[i] * (min(i + 1, end) - max(i, start))
                i += 1
            out.append(total / scale)
        result.append(out)
    return result


def main():
    if len(sys.argv) != 2:
        raise SystemExit('usage: python tools\\make_glint.py <star.dds>')
    width, height, alpha = read_alpha(sys.argv[1])
    rows = resample(alpha, SIZE)
    columns = resample([list(c) for c in zip(*rows)], SIZE)
    image = [list(r) for r in zip(*columns)]
    root = os.path.join(os.path.dirname(os.path.abspath(__file__)), '..', 'data', 'textures', 'OStrands')
    os.makedirs(root, exist_ok=True)
    out = os.path.join(root, 'glint.dds')
    pixel = lambda x, y: (255, 255, 255, max(0, min(255, int(round(image[y][x])))))
    open(out, 'wb').write(dds_bgra(SIZE, SIZE, pixel))
    print('glint.dds: %dx%d from %dx%d %s' % (SIZE, SIZE, width, height, os.path.basename(sys.argv[1])))


if __name__ == '__main__':
    main()
