/* dwgload.c - manual binding of the LibreDWG shared library.
   Part of DWG Viewer plugin for Total Commander (WLX).
   GPLv3+ */
#include <stdlib.h>
#include <wchar.h>
#include "dwgload.h"

HMODULE dwg_module;

typedef int (*fn_read_file)(const char *, Dwg_Data *);
typedef void (*fn_free)(Dwg_Data *);
typedef const char *(*fn_version_type)(Dwg_Version_Type);
typedef Dwg_Object *(*fn_ref_object)(Dwg_Data *, Dwg_Object_Ref *);
typedef Dwg_Object *(*fn_first_owned)(const Dwg_Object *);
typedef Dwg_Object *(*fn_next_owned)(const Dwg_Object *, const Dwg_Object *);
typedef Dwg_Object_Ref *(*fn_space_ref)(Dwg_Data *);
typedef int (*fn_utf8text)(void *, const char *, const char *, char **, int *,
                           void *);

static fn_read_file p_read_file;
static fn_free p_free;
static fn_version_type p_version_type;
static fn_ref_object p_ref_object;
static fn_first_owned p_first_owned;
static fn_next_owned p_next_owned;
static fn_space_ref p_model_space_ref;
static fn_space_ref p_paper_space_ref;
static fn_utf8text p_utf8text;

static int g_tried; /* 0 = not tried yet, 1 = ok, -1 = failed */
static wchar_t g_error[128];

static void
lib_init(void)
{
  HMODULE lib;
  wchar_t path[MAX_PATH];

  if (g_tried != 0)
    return;
  g_tried = -1;

  if (dwg_module) {
    GetModuleFileNameW(dwg_module, path, MAX_PATH);
    wchar_t *slash = wcsrchr(path, L'\\');
    if (slash)
      *(slash + 1) = 0;
    wcscat(path, L"libredwg-0.dll");
    lib = LoadLibraryW(path);
  } else {
    lib = NULL;
  }
  if (!lib)
    lib = LoadLibraryW(L"libredwg-0.dll"); /* fall back to search path */
  if (!lib) {
    _snwprintf(g_error, 128, L"libredwg-0.dll не найдена в каталоге плагина");
    return;
  }

  p_read_file =
      (fn_read_file)GetProcAddress(lib, "dwg_read_file");
  p_free = (fn_free)GetProcAddress(lib, "dwg_free");
  p_version_type =
      (fn_version_type)GetProcAddress(lib, "dwg_version_type");
  p_ref_object = (fn_ref_object)GetProcAddress(lib, "dwg_ref_object");
  p_first_owned = (fn_first_owned)GetProcAddress(lib, "get_first_owned_entity");
  p_next_owned = (fn_next_owned)GetProcAddress(lib, "get_next_owned_entity");
  p_model_space_ref =
      (fn_space_ref)GetProcAddress(lib, "dwg_model_space_ref");
  p_paper_space_ref =
      (fn_space_ref)GetProcAddress(lib, "dwg_paper_space_ref");
  p_utf8text = (fn_utf8text)GetProcAddress(lib, "dwg_dynapi_entity_utf8text");

  if (!p_read_file || !p_free || !p_version_type || !p_ref_object ||
      !p_first_owned || !p_next_owned || !p_model_space_ref ||
      !p_paper_space_ref) {
    _snwprintf(g_error, 128, L"libredwg-0.dll: не все функции найдены");
    return;
  }
  g_tried = 1;
}

const wchar_t *
dwg_lib_error(void)
{
  if (g_tried == 0)
    lib_init();
  return g_tried < 0 ? g_error : NULL;
}

int
dwg_read_file(const char *restrict filename, Dwg_Data *restrict dwg)
{
  lib_init();
  if (g_tried < 0 || !p_read_file)
    return 4096; /* DWG_ERR_CRITICAL */
  return p_read_file(filename, dwg);
}

void
dwg_free(Dwg_Data *restrict dwg)
{
  lib_init();
  if (g_tried > 0 && p_free)
    p_free(dwg);
}

const char *
dwg_version_type(const Dwg_Version_Type version)
{
  lib_init();
  if (g_tried > 0 && p_version_type)
    return p_version_type(version);
  return "unknown";
}

Dwg_Object *
dwg_ref_object(Dwg_Data *restrict dwg, Dwg_Object_Ref *restrict ref)
{
  lib_init();
  if (g_tried < 0 || !p_ref_object)
    return NULL;
  return p_ref_object(dwg, ref);
}

Dwg_Object *
get_first_owned_entity(const Dwg_Object *restrict hdr)
{
  lib_init();
  if (g_tried < 0 || !p_first_owned)
    return NULL;
  return p_first_owned(hdr);
}

Dwg_Object *
get_next_owned_entity(const Dwg_Object *restrict hdr,
                      const Dwg_Object *restrict current)
{
  lib_init();
  if (g_tried < 0 || !p_next_owned)
    return NULL;
  return p_next_owned(hdr, current);
}

Dwg_Object_Ref *
dwg_model_space_ref(Dwg_Data *restrict dwg)
{
  lib_init();
  if (g_tried < 0 || !p_model_space_ref)
    return NULL;
  return p_model_space_ref(dwg);
}

Dwg_Object_Ref *
dwg_paper_space_ref(Dwg_Data *restrict dwg)
{
  lib_init();
  if (g_tried < 0 || !p_paper_space_ref)
    return NULL;
  return p_paper_space_ref(dwg);
}

int
dwg_utf8text(void *entity, const char *name, const char *field, char **textp,
             int *isnew)
{
  lib_init();
  if (isnew)
    *isnew = 0;
  if (textp)
    *textp = NULL;
  if (g_tried < 0 || !p_utf8text || !entity || !name || !field)
    return 0;
  return p_utf8text(entity, name, field, textp, isnew, NULL) ? 1 : 0;
}