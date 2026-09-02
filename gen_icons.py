import os, struct, zlib

def create_png(w, h, bg_r, bg_g, bg_b, fg_r, fg_g, fg_b):
    pixels = []
    cx, cy = w / 2.0, h / 2.0
    outer_r = min(w, h) / 2.0 - 1
    inner_r = outer_r * 0.55
    for y in range(h):
        row = [0]
        for x in range(w):
            dx, dy = x - cx, y - cy
            dist = (dx*dx + dy*dy) ** 0.5
            if dist <= inner_r:
                row.extend([fg_r, fg_g, fg_b, 255])
            elif dist <= outer_r:
                row.extend([bg_r, bg_g, bg_b, 255])
            else:
                row.extend([0, 0, 0, 0])
        pixels.append(bytes(row))
    raw = b''.join(pixels)
    def chunk(ct, data):
        c = ct + data
        return struct.pack('>I', len(data)) + c + struct.pack('>I', zlib.crc32(c) & 0xffffffff)
    ihdr = struct.pack('>IIBBBBB', w, h, 8, 6, 0, 0, 0)
    return b'\x89PNG\r\n\x1a\n' + chunk(b'IHDR', ihdr) + chunk(b'IDAT', zlib.compress(raw)) + chunk(b'IEND', b'')

sizes = {'mipmap-mdpi': 48, 'mipmap-hdpi': 72, 'mipmap-xhdpi': 96, 'mipmap-xxhdpi': 144, 'mipmap-xxxhdpi': 192}
base = r'C:\mystream\QualcommHRNG\app\src\main\res'
for folder, size in sizes.items():
    d = os.path.join(base, folder)
    os.makedirs(d, exist_ok=True)
    png = create_png(size, size, 233, 69, 96, 83, 52, 131)
    for name in ['ic_launcher.png', 'ic_launcher_round.png']:
        with open(os.path.join(d, name), 'wb') as f:
            f.write(png)
    print(f'Created {folder}: {size}x{size}')
print('Done')