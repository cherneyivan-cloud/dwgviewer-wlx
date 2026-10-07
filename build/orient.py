import struct, zlib, sys

def read_png(path):
    d = open(path, 'rb').read()
    assert d[:8] == b'\x89PNG\r\n\x1a\n'
    i = 8
    idat = b''
    w = h = bd = ct = None
    while i < len(d):
        ln = struct.unpack('>I', d[i:i+4])[0]
        typ = d[i+4:i+8]
        chunk = d[i+8:i+8+ln]
        if typ == b'IHDR':
            w, h, bd, ct, comp, filt, inter = struct.unpack('>IIBBBBB', chunk)
        elif typ == b'IDAT':
            idat += chunk
        i += 12 + ln
    raw = zlib.decompress(idat)
    # разбор каналов
    ch = {0:1, 2:3, 3:1, 4:2, 6:4}[ct]
    bpp = max(1, ch * bd // 8)
    stride = (w * ch * bd + 7) // 8
    out = bytearray(h * stride)
    prev = bytearray(stride)
    pos = 0
    for y in range(h):
        f = raw[pos]; pos += 1
        line = bytearray(raw[pos:pos+stride]); pos += stride
        if f == 1:
            for x in range(bpp, stride): line[x] = (line[x] + line[x-bpp]) & 255
        elif f == 2:
            for x in range(stride): line[x] = (line[x] + prev[x]) & 255
        elif f == 3:
            for x in range(stride):
                a = line[x-bpp] if x >= bpp else 0
                line[x] = (line[x] + ((a + prev[x]) >> 1)) & 255
        elif f == 4:
            for x in range(stride):
                a = line[x-bpp] if x >= bpp else 0
                b = prev[x]
                c = prev[x-bpp] if x >= bpp else 0
                p = a + b - c
                pa, pb, pc = abs(p-a), abs(p-b), abs(p-c)
                pr = a if (pa <= pb and pa <= pc) else (b if pb <= pc else c)
                line[x] = (line[x] + pr) & 255
        out[y*stride:(y+1)*stride] = line
        prev = line
    return w, h, ch, bd, ct, stride, bytes(out)

def png_mask(path):
    w, h, ch, bd, ct, stride, px = read_png(path)
    # берём "яркость" каждого пикселя (поддержка 8-битных RGB/RGBA/gray)
    def val(x, y):
        o = y*stride + x*ch
        if ch >= 3:
            return px[o] + px[o+1] + px[o+2]
        return px[o]*3
    # фон = самый частый уровень
    from collections import Counter
    c = Counter(val(x, y) for y in range(h) for x in range(w))
    bg = c.most_common(1)[0][0]
    m = [[1 if abs(val(x, y) - bg) > 30 else 0 for x in range(w)] for y in range(h)]
    return w, h, m

def bmp_mask(path):
    d = open(path, 'rb').read()
    off = struct.unpack('<I', d[10:14])[0]
    w, h = struct.unpack('<ii', d[18:26])
    bpp = struct.unpack('<H', d[28:30])[0]
    top = h > 0
    h = abs(h)
    row = ((w * bpp + 31) // 32) * 4
    def px(x, y):
        yy = y if top else (h - 1 - y)
        o = off + yy*row + x*(bpp//8)
        if bpp == 24: return d[o] + d[o+1] + d[o+2]
        if bpp == 32: return d[o] + d[o+1] + d[o+2]
        return d[o]
    return w, h, [[1 if px(x, y) > 30 else 0 for x in range(w)] for y in range(h)]

def bbox(m):
    h = len(m); w = len(m[0])
    xs = [x for y in range(h) for x in range(w) if m[y][x]]
    ys = [y for y in range(h) for x in range(w) if m[y][x]]
    if not xs: return 0, 0, w, h
    return min(xs), min(ys), max(xs)+1, max(ys)+1

def norm(m, size=160):
    x0, y0, x1, y1 = bbox(m)
    cw, chh = x1-x0, y1-y0
    out = [[0]*size for _ in range(size)]
    for yy in range(size):
        for xx in range(size):
            sx = x0 + xx*cw//size
            sy = y0 + yy*chh//size
            if 0 <= sy < len(m) and 0 <= sx < len(m[0]):
                out[yy][xx] = m[sy][sx]
    return out

def transforms(m):
    h = len(m); w = len(m[0])
    ident = m
    fliph = [row[::-1] for row in m]
    flipv = m[::-1]
    rot180 = [row[::-1] for row in m[::-1]]
    return {'identity': ident, 'flipH': fliph, 'flipV': flipv, 'rot180': rot180}

def iou(a, b):
    inter = sum(1 for y in range(len(a)) for x in range(len(a[0])) if a[y][x] and b[y][x])
    uni = sum(1 for y in range(len(a)) for x in range(len(a[0])) if a[y][x] or b[y][x])
    return inter/uni if uni else 0.0

ref = norm(png_mask(sys.argv[1])[2])
our = norm(bmp_mask(sys.argv[2])[2])
print('our pixels:', sum(sum(r) for r in our), 'ref pixels:', sum(sum(r) for r in ref))
for name, t in transforms(our).items():
    print(f'  {name:9s}: IoU={iou(t, ref):.3f}')
