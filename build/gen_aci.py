import os

root = r'C:\tmp\tc wlx'

fixed = {1:(255,0,0),2:(255,255,0),3:(0,255,0),4:(0,255,255),5:(0,0,255),6:(255,0,255),7:(255,255,255),8:(128,128,128),9:(192,192,192)}
rows = [
 (255,0,0),(255,64,0),(255,128,0),(255,191,0),(255,222,0),(255,244,0),(255,255,0),(191,255,0),(128,255,0),(64,255,0),(0,255,0),(0,255,96),(0,255,160),(0,255,214),(0,255,255),(0,214,255),(0,160,255),(0,96,255),(0,0,255),(64,0,255),(128,0,255),(191,0,255),(255,0,255),(255,0,222)
]
def shade(base, k):
    f = k/9.0
    return tuple(int(base[i]+(255-base[i])*f) for i in range(3))

out = []
for i in range(1,256):
    if i in fixed:
        c = fixed[i]
    elif 250 <= i <= 255:
        v = int(255*(i-249)/6)
        c = (v,v,v)
    else:
        idx = i-10; h = idx//10; k = idx%10
        c = shade(rows[h], k)
    out.append('  0x%02X%02X%02X,  /* %3d */' % (c[2], c[1], c[0], i))
body = '\n'.join(out)

hdr = """/* aci.c - AutoCAD Color Index (ACI) palette
   Part of DWG Viewer plugin for Total Commander (WLX).
   GPLv3+ */
#include "aci.h"

/* Standard ACI 1..255 palette. COLORREF values: 0x00bbggrr. */
static const DWORD aci_palette[256] = {
  0x00FFFFFF,  /*   0  (byblock/bylayer -> white) */
""" + body + """
};

COLORREF aci_color(int index)
{
  if (index < 0)
    index = 0;
  if (index > 255)
    index = 255;
  return (COLORREF)aci_palette[index];
}
"""
with open(os.path.join(root, 'src', 'aci.c'), 'w', encoding='utf-8') as f:
    f.write(hdr)
print('aci.c written:', len(out) + 1, 'entries')