/* harness.c - end-to-end test of dwgviewer.wlx without Total Commander */
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <stdio.h>

typedef struct {
  int size;
  DWORD PluginInterfaceVersionLow;
  DWORD PluginInterfaceVersionHi;
  char DefaultIniName[MAX_PATH];
} ListDefaultParamStruct;

typedef HWND(__stdcall *ListLoadW_t)(HWND, WCHAR *, int);
typedef void(__stdcall *ListCloseWindowW_t)(HWND);
typedef void(__stdcall *ListGetDetectStringW_t)(char *, int);
typedef HBITMAP(__stdcall *ListGetPreviewBitmapW_t)(WCHAR *, int, int, char *,
                                                    int);
typedef void(__stdcall *ListSetDefaultParamsW_t)(ListDefaultParamStruct *);

static int save_hbitmap(const char *path, HBITMAP bm, int w, int h)
{
  BITMAPINFO bi;
  memset(&bi, 0, sizeof(bi));
  bi.bmiHeader.biSize = sizeof(BITMAPINFOHEADER);
  bi.bmiHeader.biWidth = w;
  bi.bmiHeader.biHeight = -h;
  bi.bmiHeader.biPlanes = 1;
  bi.bmiHeader.biBitCount = 24;
  bi.bmiHeader.biCompression = BI_RGB;
  unsigned char *px = (unsigned char *)malloc(w * h * 3 + 64);
  HDC dc = GetDC(NULL);
  if (!GetDIBits(dc, bm, 0, h, px, &bi, DIB_RGB_COLORS)) {
    ReleaseDC(NULL, dc);
    free(px);
    return 0;
  }
  ReleaseDC(NULL, dc);
  FILE *f = fopen(path, "wb");
  if (!f) {
    free(px);
    return 0;
  }
  BITMAPFILEHEADER fh;
  memset(&fh, 0, sizeof(fh));
  fh.bfType = 0x4D42;
  fh.bfOffBits = sizeof(fh) + sizeof(BITMAPINFOHEADER);
  fh.bfSize = fh.bfOffBits + w * h * 3;
  fwrite(&fh, 1, sizeof(fh), f);
  fwrite(&bi.bmiHeader, 1, sizeof(BITMAPINFOHEADER), f);
  fwrite(px, 1, (size_t)w * h * 3, f);
  fclose(f);
  free(px);
  printf("saved %s (%dx%d)\n", path, w, h);
  return 1;
}

static int save_dib24(const char *path, const BITMAPINFOHEADER *bi,
                      const void *bits32)
{
  /* convert 32bpp top-down bits to a 24bpp BMP file (row aligned to 4) */
  int w = bi->biWidth;
  int h = -bi->biHeight;
  int row24 = ((w * 3) + 3) & ~3;
  unsigned char *px = (unsigned char *)malloc(row24 * h);
  if (!px)
    return 0;
  const unsigned char *src = (const unsigned char *)bits32;
  for (int y = 0; y < h; y++) {
    const unsigned char *s = src + (size_t)y * w * 4;
    unsigned char *d = px + (size_t)y * row24;
    for (int x = 0; x < w; x++) {
      d[x * 3 + 0] = s[x * 4 + 0];
      d[x * 3 + 1] = s[x * 4 + 1];
      d[x * 3 + 2] = s[x * 4 + 2];
    }
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
  fh.bfSize = fh.bfOffBits + (DWORD)row24 * (DWORD)h;
  fwrite(&fh, 1, sizeof(fh), f);
  {
    BITMAPINFOHEADER h24 = *bi;
    h24.biBitCount = 24;
    h24.biSizeImage = (DWORD)row24 * (DWORD)h;
    fwrite(&h24, 1, sizeof(BITMAPINFOHEADER), f);
  }
  fwrite(px, 1, (size_t)row24 * (size_t)h, f);
  fclose(f);
  free(px);
  printf("saved %s (%dx%d)\n", path, w, h);
  return 1;
}

static void capture_window(HWND hwnd, int w, int h, const char *path)
{
  HDC wdc = GetDC(hwnd);
  HDC mdc = CreateCompatibleDC(wdc);
  BITMAPINFO bi;
  memset(&bi, 0, sizeof(bi));
  bi.bmiHeader.biSize = sizeof(BITMAPINFOHEADER);
  bi.bmiHeader.biWidth = w;
  bi.bmiHeader.biHeight = -h; /* top-down */
  bi.bmiHeader.biPlanes = 1;
  bi.bmiHeader.biBitCount = 32;
  bi.bmiHeader.biCompression = BI_RGB;
  void *bits = NULL;
  HBITMAP bm = CreateDIBSection(wdc, &bi, DIB_RGB_COLORS, &bits, NULL, 0);
  if (bm && bits) {
    HBITMAP old = (HBITMAP)SelectObject(mdc, bm);
    BitBlt(mdc, 0, 0, w, h, wdc, 0, 0, SRCCOPY);
    SelectObject(mdc, old);
    save_dib24(path, &bi.bmiHeader, bits);
    DeleteObject(bm);
  } else {
    printf("capture failed (%dx%d)\n", w, h);
  }
  DeleteDC(mdc);
  ReleaseDC(hwnd, wdc);
}

static LRESULT CALLBACK parent_proc(HWND hwnd, UINT msg, WPARAM wp, LPARAM lp)
{
  (void)wp;
  (void)lp;
  if (msg == WM_DESTROY) {
    PostQuitMessage(0);
    return 0;
  }
  return DefWindowProcW(hwnd, msg, wp, lp);
}

static LONG WINAPI crash_filter(EXCEPTION_POINTERS *ep)
{
  fflush(stdout);
  fprintf(stderr, "CRASH addr=%p code=0x%lx module_base=%p rva=%p\n",
          ep->ExceptionRecord->ExceptionAddress,
          ep->ExceptionRecord->ExceptionCode, GetModuleHandle(NULL),
          (void *)((uintptr_t)ep->ExceptionRecord->ExceptionAddress -
                   (uintptr_t)GetModuleHandle(NULL)));
  fflush(stderr);
  return EXCEPTION_CONTINUE_SEARCH;
}

int main(int argc, char **argv)
{
  SetUnhandledExceptionFilter(crash_filter);
  setvbuf(stdout, NULL, _IONBF, 0);
  if (argc < 2) {
    printf("usage: harness file.dwg [outdir] [plugin.wlx]\n");
    return 2;
  }
  const char *outdir = argc > 2 ? argv[2] : ".";
  const char *plugin_path = argc > 3 ? argv[3] : "dwgviewer.wlx";
  char out1[512], out2[512];
  snprintf(out1, sizeof(out1), "%s\\preview.bmp", outdir);
  snprintf(out2, sizeof(out2), "%s\\window.bmp", outdir);

  wchar_t wplugin[1024];
  MultiByteToWideChar(CP_ACP, 0, plugin_path, -1, wplugin, 1024);
  HMODULE h = LoadLibraryW(wplugin);
  if (!h) {
    printf("LoadLibrary failed (%s), code=%lu\n", plugin_path, GetLastError());
    return 1;
  }
  ListSetDefaultParamsW_t setp =
      (ListSetDefaultParamsW_t)GetProcAddress(h, "ListSetDefaultParamsW");
  ListLoadW_t load = (ListLoadW_t)GetProcAddress(h, "ListLoadW");
  ListCloseWindowW_t close =
      (ListCloseWindowW_t)GetProcAddress(h, "ListCloseWindowW");
  ListGetDetectStringW_t detect =
      (ListGetDetectStringW_t)GetProcAddress(h, "ListGetDetectStringW");
  ListGetPreviewBitmapW_t preview =
      (ListGetPreviewBitmapW_t)GetProcAddress(h, "ListGetPreviewBitmapW");
  if (!setp || !load || !close || !detect || !preview) {
    printf("missing export\n");
    return 1;
  }

  ListDefaultParamStruct dps;
  memset(&dps, 0, sizeof(dps));
  dps.size = sizeof(dps);
  dps.PluginInterfaceVersionLow = 1;
  dps.PluginInterfaceVersionHi = 2;
  strcpy(dps.DefaultIniName, "C:\\wincmd.ini");
  setp(&dps);

  char det[256];
  detect(det, sizeof(det));
  printf("detect: %s\n", det);

  WCHAR wpath[1024];
  MultiByteToWideChar(CP_ACP, 0, argv[1], -1, wpath, 1024);

  /* 1) preview bitmap */
  HBITMAP pv = preview(wpath, 320, 240, NULL, 0);
  if (pv) {
    printf("preview OK\n");
    save_hbitmap(out1, pv, 320, 240);
    DeleteObject(pv);
  } else {
    printf("preview FAILED\n");
  }

  /* 2) real lister window */
  WNDCLASSEXW wc;
  memset(&wc, 0, sizeof(wc));
  wc.cbSize = sizeof(wc);
  wc.lpfnWndProc = parent_proc;
  wc.hInstance = GetModuleHandle(NULL);
  wc.lpszClassName = L"HarnessParent";
  RegisterClassExW(&wc);

  int winw = 640, winh = 480;
  {
    const char *ew = getenv("HARNESS_W"), *eh = getenv("HARNESS_H");
    if (ew) winw = atoi(ew);
    if (eh) winh = atoi(eh);
    if (winw < 64) winw = 640;
    if (winh < 64) winh = 480;
  }
  HWND parent = CreateWindowExW(0, L"HarnessParent", L"harness", WS_OVERLAPPEDWINDOW,
                                CW_USEDEFAULT, CW_USEDEFAULT, winw, winh, NULL, NULL,
                                GetModuleHandle(NULL), NULL);
  ShowWindow(parent, SW_SHOW);
  UpdateWindow(parent);

  HWND child = load(parent, wpath, 0);
  if (!child) {
    printf("ListLoadW returned NULL\n");
    DestroyWindow(parent);
    return 1;
  }
  RECT cr;
  GetClientRect(parent, &cr);
  MoveWindow(child, 0, 0, cr.right - cr.left, cr.bottom - cr.top, TRUE);
  printf("[h after move]\n");
  /* plugin fits on need_fit during WM_SIZE; make sure a paint happens */
  UpdateWindow(child);
  printf("[h after update]\n");
  RedrawWindow(child, NULL, NULL, RDW_INVALIDATE | RDW_UPDATENOW | RDW_ALLCHILDREN);
  printf("[h after redraw]\n");
  RECT cc;
  GetClientRect(child, &cc);
  int cw = cc.right - cc.left;
  int ch = cc.bottom - cc.top;
  printf("child client: %dx%d\n", cw, ch);
  /* Зум колесом к центру (как делает пользователь), чтобы блоки/текст были
     не в субпиксельном масштабе. */
  if (!getenv("HARNESS_NOZOOM")) {
    POINT c;
    c.x = cw / 2;
    c.y = ch / 2;
    ClientToScreen(child, &c);
    for (int i = 0; i < 15; i++)
      SendMessageW(child, WM_MOUSEWHEEL, MAKEWPARAM(0, 120),
                   MAKELPARAM(c.x, c.y));
    RedrawWindow(child, NULL, NULL,
                 RDW_INVALIDATE | RDW_UPDATENOW | RDW_ALLCHILDREN);
  }
  capture_window(child, cw, ch, out2);
  printf("[h after capture]\n");

  close(child);
  printf("[h after close]\n");
  DestroyWindow(parent);
  FreeLibrary(h);
  return 0;
}