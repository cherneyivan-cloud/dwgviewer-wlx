/* minitest.c - minimal WLX plugin to verify TC can load a new plugin */
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <stdio.h>
#include <wchar.h>

typedef struct { int size; DWORD lo; DWORD hi; char ini[MAX_PATH]; } LDP;

static void stamp(const char *tag)
{
  wchar_t path[MAX_PATH];
  HMODULE h = GetModuleHandleW(L"minitest.wlx");
  GetModuleFileNameW(h ? h : NULL, path, MAX_PATH);
  wchar_t *s = wcsrchr(path, L'\\');
  if (s) *(s + 1) = 0;
  wcscat(path, L"minitest.log");
  FILE *f = _wfopen(path, L"ab");
  if (!f) return;
  fwrite(tag, 1, (size_t)strlen(tag), f);
  fwrite("\n", 1, 1, f);
  fclose(f);
}

HWND __stdcall ListLoadW(HWND p, WCHAR *f, int fl) { (void)p;(void)f;(void)fl; stamp("ListLoadW"); return NULL; }
void __stdcall ListCloseWindowW(HWND w) { (void)w; stamp("close"); }
void __stdcall ListGetDetectStringW(char *d, int m)
{ if (d && m > 1) { strncpy(d, "EXT=\"XYZ\"", m - 1); } }
int __stdcall ListSearchTextW(HWND w, WCHAR *s, int p) { return 0; }
int __stdcall ListSendCommandW(HWND w, int c, int p) { return 0; }
int __stdcall ListPrintW(HWND w, WCHAR *f, WCHAR *d, int fl, RECT *r) { return 0; }
int __stdcall ListNotificationReceivedW(HWND w, int m, WPARAM a, LPARAM b) { return 0; }
HBITMAP __stdcall ListGetPreviewBitmapW(WCHAR *f, int w, int h, char *c, int l) { return 0; }
void __stdcall ListSetDefaultParamsW(LDP *d) { (void)d; stamp("SetParams"); }
HWND __stdcall ListLoad(HWND p, char *f, int fl) { return NULL; }
void __stdcall ListCloseWindow(HWND w) {}
void __stdcall ListGetDetectString(char *d, int m) { if (d && m > 1) strncpy(d, "EXT=\"XYZ\"", m - 1); }
int __stdcall ListSearchText(HWND w, char *s, int p) { return 0; }
int __stdcall ListSendCommand(HWND w, int c, int p) { return 0; }
int __stdcall ListPrint(HWND w, char *f, char *d, int fl, RECT *r) { return 0; }
int __stdcall ListNotificationReceived(HWND w, int m, WPARAM a, LPARAM b) { return 0; }
HBITMAP __stdcall ListGetPreviewBitmap(char *f, int w, int h, char *c, int l) { return 0; }
void __stdcall ListSetDefaultParams(LDP *d) {}

BOOL __stdcall DllMain(HMODULE hinst, DWORD reason, LPVOID r)
{
  (void)r;
  if (reason == DLL_PROCESS_ATTACH) {
    stamp("PLUGIN LOADED");
  }
  return TRUE;
}