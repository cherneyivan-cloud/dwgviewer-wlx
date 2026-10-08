/* txtdump.c - dump TEXT/MTEXT decoded (UTF-16LE aware) */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <wchar.h>
#include "dwgload.h"
#include <dwg.h>

static int looks_utf16(const unsigned char *s, int maxn)
{
  int hi = 0, lo = 0;
  for (int i = 0; i < maxn; i += 2) {
    if (!s[i] && !s[i + 1]) break;
    if (s[i + 1] == 0) lo++;
    else hi++;
  }
  return lo > hi && lo > 0;
}

static void print_str(const char *s)
{
  if (!s) { printf("(null)\n"); return; }
  const unsigned char *b = (const unsigned char *)s;
  if (looks_utf16(b, 400)) {
    printf("UTF16: \"");
    for (int i = 0; b[i] || b[i + 1]; i += 2) {
      unsigned int c = b[i] | (b[i + 1] << 8);
      if (c >= 32 && c < 127) putchar(c);
      else if (c == 10) printf("\\n");
      else if (c == 13) ;
      else printf("[%04X]", c);
    }
    printf("\"\n");
  } else {
    printf("BYTES: \"");
    for (int i = 0; b[i] && i < 400; i++) {
      unsigned char c = b[i];
      putchar((c >= 32 && c < 127) ? c : '.');
    }
    printf("\"\n");
  }
}

int main(int argc, char **argv)
{
  if (argc < 2) return 2;
  Dwg_Data dwg; memset(&dwg, 0, sizeof(dwg));
  int err = dwg_read_file(argv[1], &dwg);
  printf("read ret=%d objects=%u\n", err, dwg.num_objects);
  int nt = 0, nm = 0;
  for (unsigned int i = 0; i < dwg.num_objects; i++) {
    Dwg_Object *o = &dwg.object[i];
    if (o->supertype != DWG_SUPERTYPE_ENTITY || !o->tio.entity) continue;
    Dwg_Object_Entity *e = o->tio.entity;
    if (o->fixedtype == DWG_TYPE_TEXT && e->tio.TEXT) {
      Dwg_Entity_TEXT *t = e->tio.TEXT;
      if (nt++ < 10) { printf("[TEXT] "); print_str(t->text_value); }
    } else if (o->fixedtype == DWG_TYPE_MTEXT && e->tio.MTEXT) {
      Dwg_Entity_MTEXT *t = e->tio.MTEXT;
      if (nm < 25) { printf("[MTEXT] "); print_str(t->text); nm++; }
    }
  }
  printf("counts TEXT=%d MTEXT(shown)=%d\n", nt, nm);
  dwg_free(&dwg);
  return 0;
}
