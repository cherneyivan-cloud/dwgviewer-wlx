/* renderblock.c - render one block (layout space) of a DWG to a BMP.
   usage: renderblock file.dwg out.bmp [block-index|-1] [width] [height]
   With -1 (default) the block whose extents look most like an A3 landscape
   sheet is chosen, so paper-space layouts can be inspected directly. */
#include <windows.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <math.h>

#include "model.h"
#include "draw.h"

static int save_bmp(const char *path, HBITMAP bm, int w, int h)
{
  BITMAPINFO bi;
  memset(&bi, 0, sizeof(bi));
  bi.bmiHeader.biSize = sizeof(BITMAPINFOHEADER);
  bi.bmiHeader.biWidth = w;
  bi.bmiHeader.biHeight = -h;
  bi.bmiHeader.biPlanes = 1;
  bi.bmiHeader.biBitCount = 24;
  bi.bmiHeader.biCompression = BI_RGB;
  unsigned char *px = (unsigned char *)malloc((size_t)w * h * 3 + 64);
  if (!px)
    return 0;
  HDC dc = GetDC(NULL);
  int ok = GetDIBits(dc, bm, 0, h, px, &bi, DIB_RGB_COLORS) != 0;
  ReleaseDC(NULL, dc);
  if (!ok) {
    free(px);
    return 0;
  }
  FILE *f = fopen(path, "wb");
  if (!f) {
    free(px);
    return 0;
  }
  BITMAPFILEHEADER fh;
  memset(&fh, 0, sizeof(fh));
  fh.bfType = 0x4D42;
  fh.bfOffBits = sizeof(fh) + sizeof(BITMAPINFOHEADER);
  fh.bfSize = fh.bfOffBits + (DWORD)(w * h * 3);
  fwrite(&fh, 1, sizeof(fh), f);
  fwrite(&bi.bmiHeader, 1, sizeof(BITMAPINFOHEADER), f);
  fwrite(px, 1, (size_t)w * h * 3, f);
  fclose(f);
  free(px);
  return 1;
}

int main(int argc, char **argv)
{
  if (argc < 3) {
    fprintf(stderr, "usage: renderblock file.dwg out.bmp [block|-1] [w] [h]\n");
    return 2;
  }
  wchar_t wpath[1024];
  MultiByteToWideChar(CP_ACP, 0, argv[1], -1, wpath, 1024);
  wchar_t err[512];
  DwgModel *m = model_load(wpath, err, 512);
  if (!m) {
    printf("load failed: %ls\n", err);
    return 1;
  }
  int want = (argc > 3) ? atoi(argv[3]) : -1;
  int W = (argc > 4) ? atoi(argv[4]) : 1600;
  int H = (argc > 5) ? atoi(argv[5]) : 1130;
  if (W < 64) W = 1600;
  if (H < 64) H = 1130;

  printf("blocks=%d texts=%lu chosen=%d\n", m->n_blocks, m->n_texts,
         m->render_block);

  if (want < 0) {
    /* pick the block whose aspect ratio is closest to A3 landscape and
       whose area is the largest (the sheet contains the most content) */
    int best = -1;
    double best_area = 0;
    for (int i = 0; i < m->n_blocks; i++) {
      model_select_block(m, i);
      if (!m->has_content)
        continue;
      double w = m->xmax - m->xmin, h = m->ymax - m->ymin;
      if (!(w > 0) || !(h > 0) || !isfinite(w) || !isfinite(h))
        continue;
      double ar = w / h;
      double score = (fabs(ar - 1.4142) < 0.25) ? w * h : w * h * 0.05;
      if (i < 6 || (fabs(ar - 1.4142) < 0.25 && w < 1000))
        printf("  block %d: w=%g h=%g ar=%.2f\n", i, w, h, ar);
      if (score > best_area) {
        best_area = score;
        best = i;
      }
    }
    want = best;
    printf("auto-selected block %d\n", want);
  }
  model_select_block(m, want);
  if (!m->has_content) {
    printf("block %d has no content\n", want);
    model_free(m);
    return 1;
  }
  if (argc > 9) {
    /* explicit model-space rectangle: x0 x1 y0 y1 */
    m->xmin = atof(argv[6]);
    m->xmax = atof(argv[7]);
    m->ymin = atof(argv[8]);
    m->ymax = atof(argv[9]);
    m->has_content = 1;
  }
  printf("extents x[%g..%g] y[%g..%g] texts=%lu\n", m->xmin, m->xmax,
         m->ymin, m->ymax, m->n_texts);

  HDC sdc = GetDC(NULL);
  HDC mdc = CreateCompatibleDC(sdc);
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
  if (!bm) {
    DeleteDC(mdc);
    ReleaseDC(NULL, sdc);
    model_free(m);
    return 1;
  }
  HBITMAP old = (HBITMAP)SelectObject(mdc, bm);
  DrawOpts o;
  memset(&o, 0, sizeof(o));
  o.bg_white = 0;
  XForm xf;
  draw_fit_xform(m, W, H, 8, &xf);
  draw_model(mdc, m, &xf, W, H, &o, 1);
  SelectObject(mdc, old);
  int ok = save_bmp(argv[2], bm, W, H);
  DeleteObject(bm);
  DeleteDC(mdc);
  ReleaseDC(NULL, sdc);
  model_free(m);
  printf(ok ? "saved %s\n" : "save failed\n", argv[2]);
  return ok ? 0 : 1;
}
