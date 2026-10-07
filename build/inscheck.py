import re, sys, collections

path = sys.argv[1]
data = open(path, 'rb').read()

# Разбор DXF: пары "код\r\nзначение\r\n"
lines = data.split(b'\r\n')
pairs = []
i = 0
while i + 1 < len(lines):
    code = lines[i].strip()
    val = lines[i+1]
    if code.isdigit() or (code.startswith(b'-') and code[1:].isdigit()):
        pairs.append((code, val))
    i += 2

def entities(pairs):
    out = []
    cur = None
    for code, val in pairs:
        if code == b'0':
            if cur: out.append(cur)
            cur = {'type': val.decode('latin1'), 'g': {}}
        elif cur is not None:
            cur['g'].setdefault(code, []).append(val.decode('latin1'))
    if cur: out.append(cur)
    return out

ents = entities(pairs)
ins = [e for e in ents if e['type'] == 'INSERT']
print('INSERT count:', len(ins))
for e in ins[:20]:
    g = e['g']
    name = g.get(b'2', ['?'])[0]
    pt = (g.get(b'10',['?'])[0], g.get(b'20',['?'])[0])
    sc = (g.get(b'41',['1'])[0], g.get(b'42',['1'])[0], g.get(b'43',['1'])[0])
    rot = g.get(b'50',['0'])[0]
    ext = (g.get(b'210',['0'])[0], g.get(b'220',['0'])[0], g.get(b'230',['1'])[0])
    print(f"  name={name} pt=({pt[0]},{pt[1]}) scale=({sc[0]},{sc[1]},{sc[2]}) rot={rot} ext=({ext[0]},{ext[1]},{ext[2]})")

# статистика масштабов
negs = [e for e in ins if float(e['g'].get(b'41',['1'])[0]) < 0 or float(e['g'].get(b'42',['1'])[0]) < 0]
print('inserts with negative scale:', len(negs))
rots = collections.Counter(e['g'].get(b'50',['0'])[0] for e in ins)
print('rotations:', rots.most_common(8))
