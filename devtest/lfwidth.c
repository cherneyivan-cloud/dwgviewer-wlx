/* lfwidth.c - calibrate CreateFont lfWidth against measured ink width */
#include <windows.h>
#include <stdio.h>

static int ink_w(HDC dc, int W, int H)
{
  int minx = W, maxx = -1;
  for (int y = 0; y < H; y++)
    for (int x = 0; x < W; x++)
      if (GetPixel(dc, x, y) != RGB(0, 0, 0)) {
        if (x < minx) minx = x;
        if (x > maxx) maxx = x;
      }
  return (maxx >= minx) ? (maxx - minx + 1) : 0;
}

static int measure(int hpx, int lfWidth)
{
  const int W = 1200, H = 200;
  HDC sdc = GetDC(NULL);
  HDC dc = CreateCompatibleDC(sdc);
  HBITMAP bm = CreateCompatibleBitmap(sdc, W, H);
  HBITMAP oldb = (HBITMAP)SelectObject(dc, bm);
  RECT r = { 0, 0, W, H };
  HBRUSH bg = CreateSolidBrush(RGB(0, 0, 0));
  FillRect(dc, &r, bg);
  DeleteObject(bg);

  HFONT f = CreateFontW(-hpx, lfWidth, 0, 0, FW_NORMAL, 0, 0, 0,
                        DEFAULT_CHARSET, OUT_TT_ONLY_PRECIS,
                        CLIP_DEFAULT_PRECIS, CLEARTYPE_QUALITY,
                        DEFAULT_PITCH | FF_DONTCARE, L"Arial");
  HFONT oldf = (HFONT)SelectObject(dc, f);
  SetBkMode(dc, TRANSPARENT);
  SetTextColor(dc, RGB(255, 255, 255));
  SetTextAlign(dc, TA_LEFT | TA_BASELINE);
  TextOutW(dc, 20, 140, L"ABCDEFGHIJ", 10);
  int w = ink_w(dc, W, H);
  SelectObject(dc, oldf);
  DeleteObject(f);
  SelectObject(dc, oldb);
  DeleteObject(bm);
  DeleteDC(dc);
  ReleaseDC(NULL, sdc);
  return w;
}

int main(void)
{
  int base = measure(60, 0);
  printf("h=60 lfWidth=0     ink=%d (base)\n", base);
  for (int lw = 10; lw <= 60; lw += 5) {
    int w = measure(60, -lw);
    printf("h=60 lfWidth=-%2d  ink=%4d ratio=%.3f\n", lw, w,
           (double)w / base);
  }
  return 0;
}
