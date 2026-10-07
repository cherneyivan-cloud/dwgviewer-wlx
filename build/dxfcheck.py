import re, collections, sys, os

path = sys.argv[1]
data = open(path, 'rb').read()
print('dxf bytes:', len(data))

vals230 = collections.Counter(re.findall(rb'\r\n230\r\n([-\d.eE+]+)\r\n', data))
print('230 (extrusion Z):', vals230.most_common(6))
vals210 = collections.Counter(re.findall(rb'\r\n210\r\n([-\d.eE+]+)\r\n', data))
print('210 (extrusion X):', vals210.most_common(4))
vals220 = collections.Counter(re.findall(rb'\r\n220\r\n([-\d.eE+]+)\r\n', data))
print('220 (extrusion Y):', vals220.most_common(4))

for key in (b'EXTMIN', b'EXTMAX', b'LIMMIN', b'LIMMAX', b'INSBASE'):
    i = data.find(key)
    if i >= 0:
        print(key.decode(), '->', repr(data[i:i+70]))
