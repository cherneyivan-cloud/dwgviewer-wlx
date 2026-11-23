/* wfcheck.c - does a GDI world transform affect TextOut? */
#include <windows.h>
#include <stdio.h>

static int ink_width(HDC dc, int w, int h)
{
  int minx = w, maxx = -1;
  for (int y = 0; y < h; y++)
    for (int x = 0; x < w; x++)
      if (GetPixel(dc, x, y) != RGB(0, 0, 0)) {
        if (x < minx) minx = x;
        if (x > maxx) maxx = x;
      }
  return (maxx >= minx) ? (maxx - minx + 1) : 0;
}

static void run(double wf, int text)
{
  const int W = 2400, H = 260;
  HDC sdc = GetDC(NULL);
  HDC dc = CreateCompatibleDC(sdc);
  HBITMAP bm = CreateCompatibleBitmap(sdc, W, H);
  HBITMAP oldb = (HBITMAP)SelectObject(dc, bm);
  RECT r = { 0, 0, W, H };
  HBRUSH b = CreateSolidBrush(RGB(0, 0, 0));
  FillRect(dc, &r, b);
  DeleteObject(b);

  int gm = 0, wt = -1;
  XFORM w;
  w.eM11 = (FLOAT)wf; w.eM12 = 0; w.eM21 = 0; w.eM22 = 1;
  w.eDx = 0; w.eDy = 0;
  if (wf != 1.0) {
    gm = SetGraphicsMode(dc, GM_ADVANCED);
    wt = SetWorldTransform(dc, &w);
  }

  if (text) {
    HFONT f = CreateFontW(-120, 0, 0, 0, FW_NORMAL, 0, 0, 0, DEFAULT_CHARSET,
                          OUT_TT_PRECIS, CLIP_DEFAULT_PRECIS, CLEARTYPE_QUALITY,
                          DEFAULT_PITCH | FF_SWISS, L"Arial");
    HFONT oldf = (HFONT)SelectObject(dc, f);
    SetBkMode(dc, TRANSPARENT);
    SetTextColor(dc, RGB(255, 255, 255));
    SetTextAlign(dc, TA_LEFT | TA_BASELINE);
    TextOutW(dc, 20, 200, L"ABCDEFGHIJ", 10);
    SelectObject(dc, oldf);
    DeleteObject(f);
  } else {
    HPEN p = CreatePen(PS_SOLID, 6, RGB(255, 255, 255));
    HPEN oldp = (HPEN)SelectObject(dc, p);
    MoveToEx(dc, 20, 130, NULL);
    LineTo(dc, 1200, 130);
    SelectObject(dc, oldp);
    DeleteObject(p);
  }
  int iw = ink_width(dc, W, H);
  printf("%s wf=%.2f gm=%d wt=%d ink=%d\n", text ? "text" : "line", wf, gm, wt,
         iw);
  if (wf != 1.0) {
    ModifyWorldTransform(dc, NULL, MWT_IDENTITY);
    SetGraphicsMode(dc, GM_COMPATIBLE);
  }
  SelectObject(dc, oldb);
  DeleteObject(bm);
  DeleteDC(dc);
  ReleaseDC(NULL, sdc);
}

int main(void)
{
  run(1.0, 0);
  run(0.5, 0);
  run(1.0, 1);
  run(0.5, 1);
  return 0;
}
