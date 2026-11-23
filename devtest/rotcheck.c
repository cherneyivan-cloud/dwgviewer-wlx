/* rotcheck.c - reproduce draw_text_prim's rotated+width-factor path */
#include <windows.h>
#include <stdio.h>
#include <math.h>

static void draw(double rot_deg, double wf, const char *out)
{
  const int W = 420, H = 420;
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

  double hpx = 40.0;
  double theta = rot_deg * M_PI / 180.0;
  /* device-space baseline and "down" vectors (Y grows downward) */
  double ubx = cos(theta), uby = -sin(theta);
  double udx = sin(theta), udy = cos(theta);
  double px0 = 210, py0 = 330;

  HFONT f = CreateFontW(-(int)hpx, 0, 0, 0, FW_NORMAL, 0, 0, 0,
                        DEFAULT_CHARSET, OUT_TT_ONLY_PRECIS,
                        CLIP_DEFAULT_PRECIS, CLEARTYPE_QUALITY,
                        DEFAULT_PITCH | FF_DONTCARE, L"Arial");
  HFONT oldf = (HFONT)SelectObject(dc, f);
  SetBkMode(dc, TRANSPARENT);
  SetTextColor(dc, RGB(255, 255, 255));
  SetTextAlign(dc, TA_LEFT | TA_BASELINE | TA_NOUPDATECP);

  int wt_set = 0;
  if (wf != 1.0) {
    XFORM w;
    w.eM11 = (FLOAT)(ubx * wf);
    w.eM12 = (FLOAT)(uby * wf);
    w.eM21 = (FLOAT)udx;
    w.eM22 = (FLOAT)udy;
    w.eDx = (FLOAT)px0;
    w.eDy = (FLOAT)py0;
    if (SetGraphicsMode(dc, GM_ADVANCED) && SetWorldTransform(dc, &w))
      wt_set = 1;
  }
  if (wt_set)
    TextOutW(dc, 0, 0, L"8772", 4);
  else {
    /* fallback path used for wf == 1: escapement + device coords */
    SelectObject(dc, oldf);
    DeleteObject(f);
    int esc = (int)(-rot_deg * 10);
    f = CreateFontW(-(int)hpx, 0, esc, 0, FW_NORMAL, 0, 0, 0, DEFAULT_CHARSET,
                    OUT_TT_ONLY_PRECIS, CLIP_DEFAULT_PRECIS, CLEARTYPE_QUALITY,
                    DEFAULT_PITCH | FF_DONTCARE, L"Arial");
    SelectObject(dc, f);
    SetTextAlign(dc, TA_LEFT | TA_BASELINE | TA_NOUPDATECP);
    TextOutW(dc, (int)px0, (int)py0, L"8772", 4);
  }

  /* save 24bpp bmp */
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

  if (wt_set) {
    ModifyWorldTransform(dc, NULL, MWT_IDENTITY);
    SetGraphicsMode(dc, GM_COMPATIBLE);
  }
  SelectObject(dc, oldf);
  DeleteObject(f);
  SelectObject(dc, oldb);
  DeleteObject(bm);
  DeleteDC(dc);
  ReleaseDC(NULL, sdc);
  printf("%s: rot=%.0f wf=%.2f wt=%d\n", out, rot_deg, wf, wt_set);
}

int main(void)
{
  draw(90, 1.0, "C:\\tmp\\rot90_wf1.bmp");
  draw(90, 0.7, "C:\\tmp\\rot90_wf07.bmp");
  draw(0, 0.7, "C:\\tmp\\rot0_wf07.bmp");
  return 0;
}
