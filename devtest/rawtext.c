/* rawtext.c - dump raw text_value bytes and the font used, straight from
   LibreDWG (no intermediate decoding). */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <windows.h>
#include "dwgload.h"   /* dynamic wrappers: dwg_read_file/dwg_ref_object/... */
#include <dwg.h>

static void hexdump(const char *s, int maxlen)
{
  int n = 0;
  while (s[n] && n < maxlen)
    n++;
  for (int i = 0; i < n; i++)
    printf("%02X ", (unsigned char)s[i]);
  printf(" | ");
  for (int i = 0; i < n; i++) {
    unsigned char c = (unsigned char)s[i];
    putchar(c >= 32 && c < 127 ? c : '.');
  }
  printf("\n");
}

int main(int argc, char **argv)
{
  if (argc < 2) {
    fprintf(stderr, "usage: rawtext file.dwg\n");
    return 2;
  }
  Dwg_Data dwg;
  memset(&dwg, 0, sizeof(dwg));
  int err = dwg_read_file(argv[1], &dwg);
  printf("read ret=%d objects=%u\n", err, dwg.num_objects);
  int shown = 0;
  for (unsigned int i = 0; i < dwg.num_objects && shown < 30; i++) {
    Dwg_Object *obj = &dwg.object[i];
    if (obj->supertype != DWG_SUPERTYPE_ENTITY || !obj->tio.entity)
      continue;
    const char *tv = NULL;
    const char *kind = NULL;
    if (obj->fixedtype == DWG_TYPE_TEXT && obj->tio.entity->tio.TEXT) {
      tv = obj->tio.entity->tio.TEXT->text_value;
      kind = "TEXT";
    } else if (obj->fixedtype == DWG_TYPE_MTEXT &&
               obj->tio.entity->tio.MTEXT) {
      tv = obj->tio.entity->tio.MTEXT->text;
      kind = "MTEXT";
    } else {
      continue;
    }
    if (!tv)
      continue;
    /* resolve style -> font file */
    Dwg_Object_Ref *styref = NULL;
    if (obj->fixedtype == DWG_TYPE_TEXT)
      styref = obj->tio.entity->tio.TEXT->style;
    else
      styref = obj->tio.entity->tio.MTEXT->style;
    const char *font = "?";
    if (styref) {
      Dwg_Object *so = dwg_ref_object(&dwg, styref);
      if (so && so->fixedtype == DWG_TYPE_STYLE &&
          so->tio.object && so->tio.object->tio.STYLE &&
          so->tio.object->tio.STYLE->font_file)
        font = so->tio.object->tio.STYLE->font_file;
    }
    printf("[%s font=%s] ", kind, font);
    hexdump(tv, 64);
    shown++;
  }
  dwg_free(&dwg);
  return 0;
}