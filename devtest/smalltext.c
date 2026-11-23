/* smalltext.c - how does wf compression behave at small font sizes? */
#include <windows.h>
#include <stdio.h>
#include <math.h>

static void run(int hpx, double wf, const char *out)
{
  const int W = 260, H = 90;
  HDC sdc = GetDC(NULL);
  HDC dc = CreateCompatibleDC(sdc);
  BITMAPINFO bi;
  memset(&bi, 0, sizeof(bi));
  bi.bmiHeader.biSize = sizeof(BITMAPINFOHEADER);
  bi.bmiHeader.biWidth = W;
  bi.bmiHeader.biHeight = -H;
  bi.bmiHeader.biPlanes = 1;
  bi.bmiHeader.biBitCount = 24;
  bi.bmiHeader.biCompression = BI_RGB;
  void *bits = NULL;
  HBITMAP bm = CreateDIBSection(sdc, &bi, DIB_RGB_COLORS, &bits, NULL, 0);
  HBITMAP oldb = (HBITMAP)SelectObject(dc, bm);
  RECT r = { 0, 0, W, H };
  HBRUSH bg = CreateSolidBrush(RGB(0, 0, 0));
  FillRect(dc, &r, bg);
  DeleteObject(bg);

  HFONT f = CreateFontW(-hpx, 0, 0, 0, FW_NORMAL, 0, 0, 0, DEFAULT_CHARSET,
                        OUT_TT_ONLY_PRECIS, CLIP_DEFAULT_PRECIS,
                        CLEARTYPE_QUALITY, DEFAULT_PITCH | FF_DONTCARE,
                        L"Arial");
  HFONT oldf = (HFONT)SelectObject(dc, f);
  SetBkMode(dc, TRANSPARENT);
  SetTextColor(dc, RGB(255, 255, 255));
  SetTextAlign(dc, TA_LEFT | TA_BASELINE | TA_NOUPDATECP);

  int wt = 0;
  if (wf != 1.0) {
    XFORM w;
    w.eM11 = (FLOAT)wf;
    w.eM12 = 0;
    w.eM21 = 0;
    w.eM22 = 1;
    w.eDx = 0;
    w.eDy = 0;
    if (SetGraphicsMode(dc, GM_ADVANCED) && SetWorldTransform(dc, &w))
      wt = 1;
  }
  TextOutW(dc, 20, 65, L"1  2  2  1", 10);
  if (wt) {
    ModifyWorldTransform(dc, NULL, MWT_IDENTITY);
    SetGraphicsMode(dc, GM_COMPATIBLE);
  }

  unsigned char *px = (unsigned char *)malloc((size_t)W * H * 3);
  BITMAPINFO gb;
  memset(&gb, 0, sizeof(gb));
  gb.bmiHeader.biSize = sizeof(BITMAPINFOHEADER);
  gb.bmiHeader.biWidth = W;
  gb.bmiHeader.biHeight = -H;
  gb.bmiHeader.biPlanes = 1;
  gb.bmiHeader.biBitCount = 24;
  gb.bmiHeader.biCompression = BI_RGB;
  GetDIBits(dc, bm, 0, H, px, &gb, DIB_RGB_COLORS);
  FILE *fp = fopen(out, "wb");
  if (fp) {
    BITMAPFILEHEADER fh;
    memset(&fh, 0, sizeof(fh));
    fh.bfType = 0x4D42;
    fh.bfOffBits = sizeof(fh) + sizeof(BITMAPINFOHEADER);
    fh.bfSize = fh.bfOffBits + (DWORD)(W * H * 3);
    fwrite(&fh, 1, sizeof(fh), fp);
    fwrite(&gb.bmiHeader, 1, sizeof(BITMAPINFOHEADER), fp);
    fwrite(px, 1, (size_t)W * H * 3, fp);
    fclose(fp);
  }
  free(px);
  SelectObject(dc, oldf);
  DeleteObject(f);
  SelectObject(dc, oldb);
  DeleteObject(bm);
  DeleteDC(dc);
  ReleaseDC(NULL, sdc);
  printf("%s h=%d wf=%.2f wt=%d\n", out, hpx, wf, wt);
}

int main(void)
{
  run(24, 0.7, "C:\\tmp\\st24.bmp");
  run(16, 0.7, "C:\\tmp\\st16.bmp");
  run(11, 0.7, "C:\\tmp\\st11.bmp");
  run(11, 1.0, "C:\\tmp\\st11n.bmp");
  return 0;
}
