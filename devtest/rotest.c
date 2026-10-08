/* rotest.c - how GDI rotates text for various escapement values */
#include <windows.h>
#include <stdio.h>

static void save(const char *path, int w, int h, void *bits)
{
  FILE *f = fopen(path, "wb");
  BITMAPFILEHEADER fh; memset(&fh, 0, sizeof(fh));
  fh.bfType = 0x4D42;
  fh.bfOffBits = sizeof(fh) + sizeof(BITMAPINFOHEADER);
  int row = w * 4;
  fh.bfSize = fh.bfOffBits + row * h;
  BITMAPINFOHEADER ih; memset(&ih, 0, sizeof(ih));
  ih.biSize = sizeof(ih); ih.biWidth = w; ih.biHeight = h; ih.biPlanes = 1;
  ih.biBitCount = 32; ih.biCompression = BI_RGB;
  fwrite(&fh, 1, sizeof(fh), f);
  fwrite(&ih, 1, sizeof(ih), f);
  fwrite(bits, 1, (size_t)row * h, f);
  fclose(f);
}

static void render(int esc, const char *path)
{
  int w = 300, h = 300;
  HDC dc = CreateCompatibleDC(NULL);
  BITMAPINFO bi; memset(&bi, 0, sizeof(bi));
  bi.bmiHeader.biSize = sizeof(BITMAPINFOHEADER);
  bi.bmiHeader.biWidth = w; bi.bmiHeader.biHeight = h; /* bottom-up */
  bi.bmiHeader.biPlanes = 1; bi.bmiHeader.biBitCount = 32;
  bi.bmiHeader.biCompression = BI_RGB;
  void *bits = NULL;
  HBITMAP bm = CreateDIBSection(dc, &bi, DIB_RGB_COLORS, &bits, NULL, 0);
  HBITMAP ob = (HBITMAP)SelectObject(dc, bm);
  /* fill white */
  RECT r = {0,0,w,h};
  HBRUSH wb = CreateSolidBrush(RGB(255,255,255));
  FillRect(dc, &r, wb);
  DeleteObject(wb);
  HFONT f = CreateFontW(-40, 0, esc, 0, FW_NORMAL, 0, 0, 0, DEFAULT_CHARSET,
                        0, 0, CLEARTYPE_QUALITY, 0, L"Arial");
  HFONT of = (HFONT)SelectObject(dc, f);
  SetBkMode(dc, TRANSPARENT);
  SetTextColor(dc, RGB(0,0,0));
  SetTextAlign(dc, TA_LEFT | TA_BASELINE);
  TextOutW(dc, 150, 150, L"ABC", 3);
  SelectObject(dc, of);
  DeleteObject(f);
  save(path, w, h, bits);
  SelectObject(dc, ob);
  DeleteObject(bm);
  DeleteDC(dc);
}

int main(void)
{
  render(0, "rot0.bmp");
  render(900, "rot900.bmp");
  render(2700, "rot2700.bmp");
  printf("done\n");
  return 0;
}
