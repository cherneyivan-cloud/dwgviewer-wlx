/* dwgload.h - manual binding of the LibreDWG shared library.
   Part of DWG Viewer plugin for Total Commander (WLX).
   GPLv3+
   Total Commander does not add the plugin directory to the Windows DLL
   search path, so a statically imported libredwg-0.dll would not be found.
   Instead we keep the same call API but resolve the functions at runtime
   from libredwg-0.dll which lives next to this plugin. */
#ifndef DWGWLX_DWGLOAD_H
#define DWGWLX_DWGLOAD_H

#include <windows.h>
#include <dwg.h>

#ifdef __cplusplus
extern "C" {
#endif

/* set by DllMain so we can locate our own directory */
extern HMODULE dwg_module;

/* wrappers with the same names/signatures as the LibreDWG exports */
int dwg_read_file(const char *restrict filename, Dwg_Data *restrict dwg);
void dwg_free(Dwg_Data *restrict dwg);
const char *dwg_version_type(const Dwg_Version_Type version);
Dwg_Object *dwg_ref_object(Dwg_Data *restrict dwg, Dwg_Object_Ref *restrict ref);
Dwg_Object *get_first_owned_entity(const Dwg_Object *restrict hdr);
Dwg_Object *get_next_owned_entity(const Dwg_Object *restrict hdr,
                                  const Dwg_Object *restrict current);
Dwg_Object_Ref *dwg_model_space_ref(Dwg_Data *restrict dwg);
Dwg_Object_Ref *dwg_paper_space_ref(Dwg_Data *restrict dwg);

/* non-NULL error text if the runtime library could not be loaded */
const wchar_t *dwg_lib_error(void);

#ifdef __cplusplus
}
#endif
#endif