/* scanraw.c - scan ALL texts in a DWG; print those that look non-UTF8
   (contain 0x7C '|' sequences, control bytes, or UTF-16LE patterns). */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <windows.h>
#include "dwgload.h"
#include <dwg.h>

static int looks_utf16le(const unsigned char *s, int n)
{
  if (n < 4)
    return 0;
  int pairs = 0, good = 0;
  for (int i = 0; i + 1 < n; i += 2) {
    unsigned char hi = s[i + 1];
    if (hi == 0x00 || hi == 0x04 || hi == 0x05)
      good++;
    pairs++;
  }
  return pairs > 0 && (good * 100 / pairs) >= 60;
}

int main(int argc, char **argv)
{
  if (argc < 2) return 2;
  Dwg_Data dwg;
  memset(&dwg, 0, sizeof(dwg));
  if (dwg_read_file(argv[1], &dwg) >= DWG_ERR_CRITICAL) {
    printf("read error\n");
    return 1;
  }
  int nt = 0, npiped = 0, nu16 = 0;
  for (unsigned int i = 0; i < dwg.num_objects; i++) {
    Dwg_Object *obj = &dwg.object[i];
    if (obj->supertype != DWG_SUPERTYPE_ENTITY || !obj->tio.entity)
      continue;
    const char *tv = NULL;
    if (obj->fixedtype == DWG_TYPE_TEXT && obj->tio.entity->tio.TEXT)
      tv = obj->tio.entity->tio.TEXT->text_value;
    else if (obj->fixedtype == DWG_TYPE_MTEXT && obj->tio.entity->tio.MTEXT)
      tv = obj->tio.entity->tio.MTEXT->text;
    else
      continue;
    if (!tv)
      continue;
    nt++;
    int n = 0;
    while (tv[n] && n < 256)
      n++;
    int pipe = 0;
    for (int k = 0; k < n; k++)
      if ((unsigned char)tv[k] == 0x7C)
        pipe = 1;
    int u16 = looks_utf16le((const unsigned char *)tv, n);
    if (pipe && npiped < 5) {
      printf("PIPE: ");
      for (int k = 0; k < n && k < 40; k++)
        printf("%02X ", (unsigned char)tv[k]);
      printf(" | ");
      for (int k = 0; k < n && k < 40; k++) {
        unsigned char c = (unsigned char)tv[k];
        putchar(c >= 32 && c < 127 ? c : '.');
      }
      printf("\n");
      npiped++;
    }
    if (u16)
      nu16++;
  }
  printf("texts=%d pipe=%d utf16le=%d\n", nt, npiped, nu16);
  dwg_free(&dwg);
  return 0;
}