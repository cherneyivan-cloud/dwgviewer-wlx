/* aci.h - AutoCAD Color Index (ACI) palette
   Part of DWG Viewer plugin for Total Commander (WLX).
   GPLv3+ */
#ifndef DWGWLX_ACI_H
#define DWGWLX_ACI_H

#include <windows.h>

/* Return COLORREF for ACI index 0..255 (0 -> white).
   COLORREF is 0x00bbggrr. */
COLORREF aci_color(int index);

#endif