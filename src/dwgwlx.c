/* dwgwlx.c - Total Commander Lister (WLX) plugin: DWG viewer
   Part of DWG Viewer plugin for Total Commander (WLX).
   GPLv3+ */
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <commdlg.h>
#include <stdlib.h>
#include <string.h>
#include <stdio.h>
#include <math.h>
#include <wchar.h>

#include "model.h"
#include "draw.h"
#include "dwgload.h"

/* ------------------------------------------------------------------ */
/* WLX interface constants (from wlx.h in the TC SDK)                  */

#define lcp_wraptext     1
#define lcp_fittowindow  2
#define lcp_ansi         4
#define lcp_ascii        8
#define lcp_variable     12
#define lcp_forceshow    16

#define lcs_findfirst    1
#define lcs_matchcase    2
#define lcs_wholewords   4
#define lcs_backwards    8

#define lc_copy          1
#define lc_newparams     2
#define lc_selectall     3
#define lc_setpercent    4

#define LISTPLUGIN_OK    0
#define LISTPLUGIN_ERROR 1

typedef struct {
  int size;
  DWORD PluginInterfaceVersionLow;
  DWORD PluginInterfaceVersionHi;
  char DefaultIniName[MAX_PATH];
} ListDefaultParamStruct;

/* ------------------------------------------------------------------ */

#define VIEW_CLASS L"TC_DWGViewer_Child"

static HMODULE g_dll_handle;

typedef struct {
  HWND hWnd;
  WCHAR file[MAX_PATH];
  DwgModel *model;
  int bg_white;

  XForm view;      /* model -> device; b == c == 0 (only zoom + pan) */
  int need_fit;
  double fit_scale;

  int dragging;
  int prev_x, prev_y;

  /* content device rect (recomputed from view) */
  RECT dev_content;
} ViewCtx;

static wchar_t g_ini_path[MAX_PATH];
static int g_class_registered;

/* ------------------------------------------------------------------ */
/* helpers                                                             */

static void ctx_fit(ViewCtx *v)
{
  RECT rc;
  GetClientRect(v->hWnd, &rc);
  int cw = rc.right - rc.left;
  int ch = rc.bottom - rc.top;
  if (cw < 8)
    cw = 8;
  if (ch < 8)
    ch = 8;
  XForm xf;
  if (!draw_fit_xform(v->model, cw, ch, 12, &xf)) {
    XForm *t = &v->view;
    t->a = t->d = 1.0;
    t->b = t->c = 0.0;
    t->e = 4;
    t->f = 4;
    v->fit_scale = 1.0;
  } else {
    v->view = xf;
    v->fit_scale = fabs(xf.a);
  }
  v->need_fit = 0;
  InvalidateRect(v->hWnd, NULL, FALSE);
}

static void dev_content_rect(ViewCtx *v)
{
  RECT rc;
  GetClientRect(v->hWnd, &rc);
  int cw = rc.right - rc.left;
  int ch = rc.bottom - rc.top;
  double x0, y0, x1, y1;
  if (v->model && v->model->has_content) {
    xf_apply(&v->view, v->model->xmin, v->model->ymin, &x0, &y0);
    xf_apply(&v->view, v->model->xmax, v->model->ymax, &x1, &y1);
    v->dev_content.left = (LONG)floor(x0 < x1 ? x0 : x1);
    v->dev_content.right = (LONG)ceil(x0 > x1 ? x0 : x1);
    v->dev_content.top = (LONG)floor(y0 < y1 ? y0 : y1);
    v->dev_content.bottom = (LONG)ceil(y0 > y1 ? y0 : y1);
  } else {
    v->dev_content.left = v->dev_content.top = 0;
    v->dev_content.right = (LONG)cw;
    v->dev_content.bottom = (LONG)ch;
  }
}

static void update_scrollbars(ViewCtx *v)
{
  RECT rc;
  GetClientRect(v->hWnd, &rc);
  int cw = rc.right - rc.left;
  int ch = rc.bottom - rc.top;
  dev_content_rect(v);
  int cw_c = v->dev_content.right - v->dev_content.left;
  int ch_c = v->dev_content.bottom - v->dev_content.top;

  SCROLLINFO si;
  memset(&si, 0, sizeof(si));
  si.cbSize = sizeof(si);
  si.fMask = SIF_RANGE | SIF_PAGE | SIF_POS | SIF_DISABLENOSCROLL;
  if (cw_c <= cw) {
    si.nMin = 0;
    si.nMax = 0;
    si.nPage = cw;
    si.nPos = 0;
  } else {
    si.nMin = 0;
    si.nMax = cw_c - cw;
    si.nPage = cw;
    double pad = (cw - cw_c) / 2.0;
    double p = v->dev_content.left + pad - v->view.e;
    si.nPos = (int)(p + 0.5);
  }
  SetScrollInfo(v->hWnd, SB_HORZ, &si, TRUE);

  if (ch_c <= ch) {
    si.nMin = 0;
    si.nMax = 0;
    si.nPage = ch;
    si.nPos = 0;
  } else {
    si.nMin = 0;
    si.nMax = ch_c - ch;
    si.nPage = ch;
    double pad = (ch - ch_c) / 2.0;
    double p = v->dev_content.top + pad - v->view.f;
    si.nPos = (int)(p + 0.5);
  }
  SetScrollInfo(v->hWnd, SB_VERT, &si, TRUE);
}

static void scroll_horizontal(ViewCtx *v, int wparam)
{
  SCROLLINFO si;
  memset(&si, 0, sizeof(si));
  si.cbSize = sizeof(si);
  si.fMask = SIF_POS | SIF_RANGE | SIF_PAGE | SIF_TRACKPOS;
  GetScrollInfo(v->hWnd, SB_HORZ, &si);
  int pos = si.nPos;
  int max = si.nMax - (int)si.nPage + 1;
  if (max < 0)
    max = 0;
  switch (LOWORD(wparam)) {
  case SB_LEFT: pos = 0; break;
  case SB_RIGHT: pos = max; break;
  case SB_LINELEFT: pos -= max / 20; break;
  case SB_LINERIGHT: pos += max / 20; break;
  case SB_PAGELEFT: pos -= (int)si.nPage; break;
  case SB_PAGERIGHT: pos += (int)si.nPage; break;
  case SB_THUMBPOSITION:
  case SB_THUMBTRACK:
    pos = (int)si.nTrackPos;
    break;
  default: return;
  }
  if (pos < 0)
    pos = 0;
  if (pos > max)
    pos = max;
  si.fMask = SIF_POS;
  si.nPos = pos;
  SetScrollInfo(v->hWnd, SB_HORZ, &si, TRUE);
  int cw_c = v->dev_content.right - v->dev_content.left;
  RECT rc;
  GetClientRect(v->hWnd, &rc);
  int cw = rc.right - rc.left;
  double pad = (cw - cw_c) / 2.0;
  v->view.e = v->dev_content.left + pad - pos;
  InvalidateRect(v->hWnd, NULL, FALSE);
}

static void scroll_vertical(ViewCtx *v, int wparam)
{
  SCROLLINFO si;
  memset(&si, 0, sizeof(si));
  si.cbSize = sizeof(si);
  si.fMask = SIF_POS | SIF_RANGE | SIF_PAGE | SIF_TRACKPOS;
  GetScrollInfo(v->hWnd, SB_VERT, &si);
  int pos = si.nPos;
  int max = si.nMax - (int)si.nPage + 1;
  if (max < 0)
    max = 0;
  switch (LOWORD(wparam)) {
  case SB_TOP: pos = 0; break;
  case SB_BOTTOM: pos = max; break;
  case SB_LINEUP: pos -= max / 20; break;
  case SB_LINEDOWN: pos += max / 20; break;
  case SB_PAGEUP: pos -= (int)si.nPage; break;
  case SB_PAGEDOWN: pos += (int)si.nPage; break;
  case SB_THUMBPOSITION:
  case SB_THUMBTRACK:
    pos = (int)si.nTrackPos;
    break;
  default: return;
  }
  if (pos < 0)
    pos = 0;
  if (pos > max)
    pos = max;
  si.fMask = SIF_POS;
  si.nPos = pos;
  SetScrollInfo(v->hWnd, SB_VERT, &si, TRUE);
  int ch_c = v->dev_content.bottom - v->dev_content.top;
  RECT rc;
  GetClientRect(v->hWnd, &rc);
  int ch = rc.bottom - rc.top;
  double pad = (ch - ch_c) / 2.0;
  v->view.f = v->dev_content.top + pad - pos;
  InvalidateRect(v->hWnd, NULL, FALSE);
}

static void zoom_at(ViewCtx *v, int mx, int my, double factor)
{
  if (factor <= 0)
    return;
  double minz = v->fit_scale * 1e-6;
  double maxz = v->fit_scale * 1e6;
  double newa = v->view.a * factor;
  if (newa < minz || newa > maxz)
    return;
  /* model point under cursor must stay fixed */
  if (v->view.a != 0.0) {
    double xm = ((double)mx - v->view.e) / v->view.a;
    v->view.e = (double)mx - newa * xm;
  }
  if (v->view.d != 0.0) {
    double ym = ((double)my - v->view.f) / v->view.d;
    v->view.f = (double)my - newa * ym;
  }
  v->view.a = newa;
  v->view.d = newa; /* isotropic */
  InvalidateRect(v->hWnd, NULL, FALSE);
}

/* ------------------------------------------------------------------ */
/* window proc                                                         */

static void copy_view_to_clipboard(ViewCtx *v)
{
  RECT rc;
  GetClientRect(v->hWnd, &rc);
  int w = rc.right - rc.left;
  int h = rc.bottom - rc.top;
  if (w < 2 || h < 2)
    return;
  HDC sdc = GetDC(v->hWnd);
  HDC mdc = CreateCompatibleDC(sdc);
  BITMAPINFO bi;
  memset(&bi, 0, sizeof(bi));
  bi.bmiHeader.biSize = sizeof(BITMAPINFOHEADER);
  bi.bmiHeader.biWidth = w;
  bi.bmiHeader.biHeight = -h;
  bi.bmiHeader.biPlanes = 1;
  bi.bmiHeader.biBitCount = 32;
  bi.bmiHeader.biCompression = BI_RGB;
  void *bits = NULL;
  HBITMAP bm = CreateDIBSection(sdc, &bi, DIB_RGB_COLORS, &bits, NULL, 0);
  if (bm && bits) {
    HBITMAP old = (HBITMAP)SelectObject(mdc, bm);
    DrawOpts o;
    memset(&o, 0, sizeof(o));
    o.bg_white = v->bg_white;
    draw_model(mdc, v->model, &v->view, w, h, &o, 1);
    SelectObject(mdc, old);

    SIZE_T hdrsz = sizeof(BITMAPINFOHEADER);
    SIZE_T pxsz = (SIZE_T)w * (SIZE_T)h * 4;
    if (OpenClipboard(v->hWnd)) {
      EmptyClipboard();
      HGLOBAL mem = GlobalAlloc(GMEM_MOVEABLE | GMEM_DDESHARE, hdrsz + pxsz);
      if (mem) {
        BYTE *dst = (BYTE *)GlobalLock(mem);
        memcpy(dst, &bi.bmiHeader, hdrsz);
        memcpy(dst + hdrsz, bits, pxsz);
        GlobalUnlock(mem);
        if (SetClipboardData(CF_DIB, mem))
          mem = NULL;
      }
      if (mem)
        GlobalFree(mem);
      CloseClipboard();
    }
  }
  if (bm)
    DeleteObject(bm);
  DeleteDC(mdc);
  ReleaseDC(v->hWnd, sdc);
}

/* ------------------------------------------------------------------ */
/* window proc                                                         */

static LRESULT CALLBACK view_proc(HWND hwnd, UINT msg, WPARAM wParam,
                                  LPARAM lParam)
{
  ViewCtx *v = (ViewCtx *)GetWindowLongPtrW(hwnd, GWLP_USERDATA);
  switch (msg) {
  case WM_PAINT: {
    PAINTSTRUCT ps;
    BeginPaint(hwnd, &ps);
    RECT rc;
    GetClientRect(hwnd, &rc);
    int w = rc.right - rc.left;
    int h = rc.bottom - rc.top;
    HDC mdc = CreateCompatibleDC(ps.hdc);
    HBITMAP bm = CreateCompatibleBitmap(ps.hdc, w, h);
    HBITMAP old = (HBITMAP)SelectObject(mdc, bm);
    DrawOpts o;
    memset(&o, 0, sizeof(o));
    o.bg_white = v ? v->bg_white : 0;
    if (v && v->model && v->model->has_content)
      draw_model(mdc, v->model, &v->view, w, h, &o, 1);
    else {
      RECT fr = { 0, 0, w, h };
      HBRUSH hb = CreateSolidBrush(v && v->bg_white ? RGB(255, 255, 255)
                                                    : RGB(0, 0, 0));
      FillRect(mdc, &fr, hb);
      DeleteObject(hb);
      const wchar_t *msg1 =
          v && v->model && !v->model->has_content
              ? L"Чертёж пуст или не содержит объектов."
              : L"Не удалось загрузить файл.";
      SetBkMode(mdc, TRANSPARENT);
      SetTextColor(mdc, v && v->bg_white ? RGB(0, 0, 0) : RGB(255, 255, 255));
      RECT tr = { 8, 8, w - 8, h - 8 };
      DrawTextW(mdc, msg1, -1, &tr, DT_LEFT | DT_TOP | DT_NOPREFIX |
                                        DT_WORDBREAK);
    }
    BitBlt(ps.hdc, 0, 0, w, h, mdc, 0, 0, SRCCOPY);
    SelectObject(mdc, old);
    DeleteObject(bm);
    DeleteDC(mdc);
    EndPaint(hwnd, &ps);
    return 0;
  }
  case WM_ERASEBKGND:
    return 1;
  case WM_SIZE:
    if (v) {
      if (v->need_fit)
        ctx_fit(v);
      update_scrollbars(v);
      InvalidateRect(hwnd, NULL, FALSE);
    }
    return 0;
  case WM_MOUSEWHEEL: {
    if (!v || !v->model)
      return 0;
    short dz = (short)(HIWORD(wParam));
    POINT pt;
    pt.x = (short)LOWORD(lParam);
    pt.y = (short)HIWORD(lParam);
    ScreenToClient(hwnd, &pt);
    double factor = dz > 0 ? 1.12 : 1.0 / 1.12;
    zoom_at(v, pt.x, pt.y, factor);
    update_scrollbars(v);
    return 0;
  }
  case WM_LBUTTONDOWN:
  case WM_MBUTTONDOWN:
  case WM_RBUTTONDOWN:
    if (v) {
      SetCapture(hwnd);
      v->dragging = 1;
      v->prev_x = (short)LOWORD(lParam);
      v->prev_y = (short)HIWORD(lParam);
    }
    return 0;
  case WM_MOUSEMOVE:
    if (v && v->dragging) {
      int x = (short)LOWORD(lParam);
      int y = (short)HIWORD(lParam);
      v->view.e += (double)(x - v->prev_x);
      v->view.f += (double)(y - v->prev_y);
      v->prev_x = x;
      v->prev_y = y;
      InvalidateRect(hwnd, NULL, FALSE);
    }
    return 0;
  case WM_LBUTTONUP:
  case WM_MBUTTONUP:
  case WM_RBUTTONUP:
    if (v) {
      v->dragging = 0;
      ReleaseCapture();
      update_scrollbars(v);
    }
    return 0;
  case WM_LBUTTONDBLCLK:
    if (v)
      ctx_fit(v);
    return 0;
  case WM_MBUTTONDBLCLK:
    if (v)
      ctx_fit(v);
    return 0;
  case WM_KEYDOWN:
    if (v) {
      RECT rc;
      GetClientRect(hwnd, &rc);
      int cx = (rc.right - rc.left) / 2;
      int cy = (rc.bottom - rc.top) / 2;
      switch (wParam) {
      case VK_ADD:
      case VK_OEM_PLUS:
        zoom_at(v, cx, cy, 1.2);
        break;
      case VK_SUBTRACT:
      case VK_OEM_MINUS:
        zoom_at(v, cx, cy, 1.0 / 1.2);
        break;
      case VK_HOME:
      case 'F':
        ctx_fit(v);
        break;
      }
    }
    return 0;
  case WM_HSCROLL:
    if (v) {
      scroll_horizontal(v, (int)wParam);
      update_scrollbars(v);
    }
    return 0;
  case WM_VSCROLL:
    if (v) {
      scroll_vertical(v, (int)wParam);
      update_scrollbars(v);
    }
    return 0;
  case WM_DESTROY:
    return 0;
  }
  return DefWindowProcW(hwnd, msg, wParam, lParam);
}

/* ------------------------------------------------------------------ */
/* loading                                                             */

/* ------------------------------------------------------------------ */
/* debug log (written next to the plugin)                              */

static void log_append(const wchar_t *line)
{
  wchar_t path[MAX_PATH];
  GetModuleFileNameW(g_dll_handle, path, MAX_PATH);
  wchar_t *slash = wcsrchr(path, L'\\');
  if (slash)
    *(slash + 1) = 0;
  wcscat(path, L"dwgviewer.log");
  FILE *f = _wfopen(path, L"ab");
  if (!f)
    return;
  /* translate wide to UTF-8 and append */
  char buf[1200];
  int n = WideCharToMultiByte(CP_UTF8, 0, line, -1, buf, sizeof(buf), NULL, NULL);
  if (n > 1) {
    fwrite(buf, 1, (size_t)n - 1, f);
    fwrite("\n", 1, 1, f);
  }
  fclose(f);
}

static ViewCtx *create_view(const WCHAR *file, HWND parent, int showflags,
                            wchar_t *return_log, int rl_len)
{
  ViewCtx *v = (ViewCtx *)calloc(1, sizeof(ViewCtx));
  if (!v)
    return NULL;
  wcsncpy(v->file, file, MAX_PATH - 1);
  v->bg_white = 0;

  wchar_t errbuf[512];
  v->model = model_load(file, errbuf, 512);

  {
    /* always write a short diagnostic line next to the plugin */
    wchar_t line[560];
    if (v->model) {
      _snwprintf(line, 560, L"OPEN %ls -> OK (v=%hs, objects=%u)", file,
                 v->model->version, v->model->obj_count);
    } else {
      _snwprintf(line, 560, L"OPEN %ls -> FAIL: %ls", file, errbuf);
    }
    log_append(line);
    if (return_log && rl_len > 0)
      _snwprintf(return_log, rl_len, L"%ls", line);
  }

  wchar_t ini[MAX_PATH];
  GetModuleFileNameW(g_dll_handle, ini, MAX_PATH);
  wchar_t *slash = wcsrchr(ini, L'\\');
  if (slash)
    *(slash + 1) = 0;
  wcscat(ini, L"dwgviewer.ini");
  int bg = GetPrivateProfileIntW(L"Options", L"Background", 0, ini);
  if (bg == 1)
    v->bg_white = 1;

  v->hWnd = CreateWindowExW(0, VIEW_CLASS, file,
                            WS_CHILD | WS_VISIBLE | WS_VSCROLL | WS_HSCROLL,
                            0, 0, 4, 4, parent, NULL, g_dll_handle, NULL);
  if (!v->hWnd) {
    /* robust fallback: create a plain OS window and install our window
       procedure manually (in case the class registration was lost) */
    int le = GetLastError();
    wchar_t logline[256];
    _snwprintf(logline, 256, L"  create window failed err=%d -> fallback", le);
    log_append(logline);
    v->hWnd = CreateWindowExW(0, L"STATIC", file,
                              WS_CHILD | WS_VISIBLE | WS_VSCROLL | WS_HSCROLL,
                              0, 0, 4, 4, parent, NULL, NULL, NULL);
    if (v->hWnd) {
      SetWindowLongPtrW(v->hWnd, GWLP_WNDPROC, (LONG_PTR)view_proc);
      SetWindowLongPtrW(v->hWnd, GWLP_HWNDPARENT, (LONG_PTR)parent);
    }
  }
  if (!v->hWnd) {
    wchar_t logline[256];
    _snwprintf(logline, 256, L"  create window failed (err=%d), ListLoadW -> NULL",
               GetLastError());
    log_append(logline);
    model_free(v->model);
    free(v);
    return NULL;
  }
  SetWindowLongPtrW(v->hWnd, GWLP_USERDATA, (LONG_PTR)v);
  v->need_fit = 1;
  return v;
}

/* ------------------------------------------------------------------ */
/* ------------------------------------------------------------------ */
/* loading                                                             */

HWND __stdcall ListLoadW(HWND parentWin, WCHAR *fileToLoad, int showFlags)
{
  wchar_t olog[600];
  wchar_t line[720];
  if (!fileToLoad) {
    log_append(L"ListLoadW: NULL file");
    return NULL;
  }
  _snwprintf(line, 720, L"ListLoadW parent=%p flags=%d file=%ls", parentWin,
             showFlags, fileToLoad);
  log_append(line);
  ViewCtx *v = create_view(fileToLoad, parentWin, showFlags, olog, 600);
  if (!v) {
    _snwprintf(line, 720, L"ListLoadW -> NULL (%ls)", olog);
    log_append(line);
    return NULL;
  }
  /* if the model could not be parsed at all, refuse so TC can try other
     plugins; if it parsed but is empty we still show a message */
  if (!v->model) {
    DestroyWindow(v->hWnd);
    free(v);
    _snwprintf(line, 720, L"ListLoadW -> NULL (model) %ls", olog);
    log_append(line);
    return NULL;
  }
  _snwprintf(line, 720, L"ListLoadW -> window %p %ls", v->hWnd, olog);
  log_append(line);
  return v->hWnd;
}

void __stdcall ListCloseWindowW(HWND listWin)
{
  ViewCtx *v = (ViewCtx *)GetWindowLongPtrW(listWin, GWLP_USERDATA);
  if (v) {
    SetWindowLongPtrW(listWin, GWLP_USERDATA, 0);
    model_free(v->model);
    free(v);
  }
  DestroyWindow(listWin);
}

void __stdcall ListGetDetectStringW(char *detectString, int maxlen)
{
  /* Simple and robust: any .dwg extension matches (plus user "force"
     via Image/Multimedia menu). DWG files of all versions R13..R2018
     are verified again in ListLoadW. */
  static const char *s = "EXT=\"DWG\" | FORCE";
  if (detectString && maxlen > 0)
    strncpy(detectString, s, (size_t)maxlen - 1);
}

int __stdcall ListSearchTextW(HWND listWin, WCHAR *searchString,
                              int searchParameter)
{
  (void)listWin;
  (void)searchString;
  (void)searchParameter;
  return LISTPLUGIN_ERROR;
}

int __stdcall ListSendCommandW(HWND listWin, int command, int parameter)
{
  ViewCtx *v = (ViewCtx *)GetWindowLongPtrW(listWin, GWLP_USERDATA);
  if (!v)
    return LISTPLUGIN_ERROR;
  switch (command) {
  case lc_copy:
    copy_view_to_clipboard(v);
    return LISTPLUGIN_OK;
  case lc_newparams:
    if (parameter & lcp_fittowindow) {
      ctx_fit(v);
      return LISTPLUGIN_OK;
    }
    return LISTPLUGIN_OK;
  case lc_selectall:
    return LISTPLUGIN_OK;
  case lc_setpercent: {
    /* pan vertically to a fraction of the content */
    if (!v->model || !v->model->has_content)
      return LISTPLUGIN_ERROR;
    int pct = parameter;
    if (pct < 0)
      pct = 0;
    if (pct > 100)
      pct = 100;
    double cy = (v->model->ymin + v->model->ymax) / 2.0;
    double span = v->model->ymax - v->model->ymin;
    double target_y = v->model->ymax - span * pct / 100.0;
    (void)cy;
    RECT rc;
    GetClientRect(listWin, &rc);
    int ch = rc.bottom - rc.top;
    double dy = v->view.d * 0.0;
    (void)dy;
    /* place target_y at vertical center of the window */
    double ym = target_y;
    v->view.f = (double)(ch / 2) - v->view.d * ym;
    InvalidateRect(listWin, NULL, FALSE);
    update_scrollbars(v);
    return LISTPLUGIN_OK;
  }
  }
  return LISTPLUGIN_ERROR;
}

int __stdcall ListPrintW(HWND listWin, WCHAR *fileToPrint, WCHAR *defPrinter,
                         int printFlags, RECT *margins)
{
  (void)fileToPrint;
  (void)defPrinter;
  (void)printFlags;
  ViewCtx *v = (ViewCtx *)GetWindowLongPtrW(listWin, GWLP_USERDATA);
  if (!v || !v->model || !v->model->has_content)
    return LISTPLUGIN_ERROR;

  PRINTDLGW pd;
  memset(&pd, 0, sizeof(pd));
  pd.lStructSize = sizeof(pd);
  pd.hwndOwner = listWin;
  pd.Flags = PD_RETURNDC | PD_NOSELECTION | PD_NOPAGENUMS |
             PD_HIDEPRINTTOFILE;
  if (!PrintDlgW(&pd))
    return LISTPLUGIN_OK; /* user cancelled */

  HDC hdc = pd.hDC;
  if (!hdc) {
    if (pd.hDevMode)
      GlobalFree(pd.hDevMode);
    if (pd.hDevNames)
      GlobalFree(pd.hDevNames);
    return LISTPLUGIN_ERROR;
  }
  int ppix = GetDeviceCaps(hdc, LOGPIXELSX);
  int ppiy = GetDeviceCaps(hdc, LOGPIXELSY);
  int paperw = GetDeviceCaps(hdc, PHYSICALWIDTH);
  int paperh = GetDeviceCaps(hdc, PHYSICALHEIGHT);
  int offx = GetDeviceCaps(hdc, PHYSICALOFFSETX);
  int offy = GetDeviceCaps(hdc, PHYSICALOFFSETY);

  /* margins are in MM_LOMETRIC (1/10 mm). 254 LM units == 1 inch.
     If margins are not provided, use a 5 mm default. */
  int lm = MulDiv(margins ? margins->left : 50, ppix, 254);
  int rm = MulDiv(margins ? margins->right : 50, ppix, 254);
  int tm = MulDiv(margins ? margins->top : 50, ppiy, 254);
  int bm = MulDiv(margins ? margins->bottom : 50, ppiy, 254);
  int mx = offx + lm;
  int my = offy + tm;
  int pw = paperw - (lm + rm);
  int ph = paperh - (tm + bm);
  if (pw < 10)
    pw = 10;
  if (ph < 10)
    ph = 10;

  DOCINFOW di;
  memset(&di, 0, sizeof(di));
  di.cbSize = sizeof(di);
  di.lpszDocName = L"DWG viewer";
  if (StartDocW(hdc, &di) > 0) {
    StartPage(hdc);
    DrawOpts o;
    memset(&o, 0, sizeof(o));
    o.bg_white = 1; /* always print on white */
    XForm xf;
    if (draw_fit_xform(v->model, pw, ph, 0, &xf)) {
      xf.e += mx;
      xf.f += my;
      draw_model(hdc, v->model, &xf, pw + mx, ph + my, &o, 1);
    }
    EndPage(hdc);
    EndDoc(hdc);
  }
  DeleteDC(hdc);
  if (pd.hDevMode)
    GlobalFree(pd.hDevMode);
  if (pd.hDevNames)
    GlobalFree(pd.hDevNames);
  return LISTPLUGIN_OK;
}

int __stdcall ListNotificationReceivedW(HWND listWin, int message,
                                        WPARAM wParam, LPARAM lParam)
{
  (void)listWin;
  (void)message;
  (void)wParam;
  (void)lParam;
  return 0;
}

HBITMAP __stdcall ListGetPreviewBitmapW(WCHAR *fileToLoad, int width,
                                        int height, char *contentBuf,
                                        int contentBufLen)
{
  (void)contentBuf;
  (void)contentBufLen;
  if (!fileToLoad || width < 16 || height < 16)
    return NULL;
  wchar_t errbuf[256];
  DwgModel *m = model_load(fileToLoad, errbuf, 256);
  if (!m || !m->has_content) {
    model_free(m);
    return NULL;
  }
  HDC sdc = GetDC(NULL);
  HDC mdc = CreateCompatibleDC(sdc);
  BITMAPINFO bi;
  memset(&bi, 0, sizeof(bi));
  bi.bmiHeader.biSize = sizeof(BITMAPINFOHEADER);
  bi.bmiHeader.biWidth = width;
  bi.bmiHeader.biHeight = -height;
  bi.bmiHeader.biPlanes = 1;
  bi.bmiHeader.biBitCount = 32;
  bi.bmiHeader.biCompression = BI_RGB;
  void *bits = NULL;
  HBITMAP bm = CreateDIBSection(sdc, &bi, DIB_RGB_COLORS, &bits, NULL, 0);
  if (bm) {
    HBITMAP old = (HBITMAP)SelectObject(mdc, bm);
    DrawOpts o;
    memset(&o, 0, sizeof(o));
    o.bg_white = 0;
    XForm xf;
    draw_fit_xform(m, width, height, 4, &xf);
    draw_model(mdc, m, &xf, width, height, &o, 0);
    SelectObject(mdc, old);
  }
  DeleteDC(mdc);
  ReleaseDC(NULL, sdc);
  model_free(m);
  return bm;
}

void __stdcall ListSetDefaultParamsW(ListDefaultParamStruct *dps)
{
  if (!dps)
    return;
  if (dps->size >= (int)sizeof(ListDefaultParamStruct) &&
      dps->DefaultIniName[0]) {
    /* The default ini is Total Commander's own wincmd.ini. We store our
       own ini next to the plugin instead. */
    MultiByteToWideChar(CP_ACP, 0, dps->DefaultIniName, -1, g_ini_path,
                        MAX_PATH);
  }
}

/* ANSI wrappers (for compatibility / 32-bit Total Commander) */

typedef HWND(__stdcall *ListLoadA_t)(HWND, char *, int);
typedef void(__stdcall *ListCloseWindowA_t)(HWND);
typedef void(__stdcall *ListGetDetectStringA_t)(char *, int);
typedef int(__stdcall *ListSearchTextA_t)(HWND, char *, int);
typedef int(__stdcall *ListSendCommandA_t)(HWND, int, int);
typedef int(__stdcall *ListPrintA_t)(HWND, char *, char *, int, RECT *);
typedef int(__stdcall *ListNotificationReceivedA_t)(HWND, int, WPARAM,
                                                    LPARAM);
typedef HBITMAP(__stdcall *ListGetPreviewBitmapA_t)(char *, int, int, char *,
                                                    int);

HWND __stdcall ListLoad(HWND parentWin, char *fileToLoad, int showFlags)
{
  WCHAR w[4096];
  if (MultiByteToWideChar(CP_ACP, 0, fileToLoad ? fileToLoad : "", -1, w,
                          4096) == 0)
    return NULL;
  return ListLoadW(parentWin, w, showFlags);
}
void __stdcall ListCloseWindow(HWND listWin) { ListCloseWindowW(listWin); }
void __stdcall ListGetDetectString(char *detectString, int maxlen)
{
  ListGetDetectStringW(detectString, maxlen);
}
int __stdcall ListSearchText(HWND listWin, char *searchString,
                             int searchParameter)
{
  return ListSearchTextW(listWin, NULL, searchParameter);
}
int __stdcall ListSendCommand(HWND listWin, int command, int parameter)
{
  return ListSendCommandW(listWin, command, parameter);
}
int __stdcall ListPrint(HWND listWin, char *fileToPrint, char *defPrinter,
                        int printFlags, RECT *margins)
{
  return ListPrintW(listWin, NULL, NULL, printFlags, margins);
}
int __stdcall ListNotificationReceived(HWND listWin, int message,
                                       WPARAM wParam, LPARAM lParam)
{
  return 0;
}
HBITMAP __stdcall ListGetPreviewBitmap(char *fileToLoad, int width,
                                       int height, char *contentBuf,
                                       int contentBufLen)
{
  WCHAR w[4096];
  if (MultiByteToWideChar(CP_ACP, 0, fileToLoad ? fileToLoad : "", -1, w,
                          4096) == 0)
    return NULL;
  return ListGetPreviewBitmapW(w, width, height, contentBuf, contentBufLen);
}
void __stdcall ListSetDefaultParams(ListDefaultParamStruct *dps)
{
  ListSetDefaultParamsW(dps);
}

static void register_view_class(void)
{
  if (g_class_registered)
    return;
  WNDCLASSEXW wc;
  memset(&wc, 0, sizeof(wc));
  wc.cbSize = sizeof(wc);
  wc.lpfnWndProc = view_proc;
  wc.hInstance = g_dll_handle;
  wc.hCursor = LoadCursor(NULL, IDC_ARROW);
  wc.hbrBackground = NULL;
  wc.lpszClassName = VIEW_CLASS;
  RegisterClassExW(&wc);
  g_class_registered = 1;
}

BOOL __stdcall DllMain(HINSTANCE hinst, DWORD reason, LPVOID reserved)
{
  (void)reserved;
  if (reason == DLL_PROCESS_ATTACH) {
    g_dll_handle = hinst;
    dwg_module = hinst; /* lets the LibreDWG loader find our own dir */
    DisableThreadLibraryCalls(hinst);
    register_view_class();
    log_append(L"PLUGIN LOADED");
  }
  return TRUE;
}