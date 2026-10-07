/* txtprobe.c - load a DWG and dump all extracted TEXT prims (UTF-8).
   Верификация декодирования русских текстов (UTF-8 vs CP1251). */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <wchar.h>
#include <windows.h>

#include "model.h"

int main(int argc, char **argv)
{
  if (argc < 2) {
    fprintf(stderr, "usage: txtprobe file.dwg\n");
    return 2;
  }
  wchar_t wpath[1024];
  MultiByteToWideChar(CP_ACP, 0, argv[1], -1, wpath, 1024);
  wchar_t errbuf[512];
  DwgModel *m = model_load(wpath, errbuf, 512);
  if (!m) {
    printf("model_load -> NULL: %ls\n", errbuf);
    return 1;
  }
  printf("version=%s objects=%u blocks=%d render=%d\n", m->version,
         m->obj_count, m->n_blocks, m->render_block);
  int shown = 0;
  for (int i = 0; i < m->n_blocks && shown < 40; i++) {
    MBlock *b = &m->blocks[i];
    if (!b->n_prims)
      continue;
    printf("-- block %d: %d prims, %d inserts\n", i, b->n_prims,
           b->n_inserts);
    for (int j = 0; j < b->n_prims; j++) {
      KPrim *p = &b->prims[j];
      if (p->kind != KP_TEXT || !p->text)
        continue;
      /* wchar -> UTF-8 for printing */
      int len = WideCharToMultiByte(CP_UTF8, 0, p->text, -1, NULL, 0, NULL,
                                    NULL);
      char *u8 = malloc(len > 0 ? (size_t)len : 1);
      WideCharToMultiByte(CP_UTF8, 0, p->text, -1, u8, len, NULL, NULL);
      printf("  text @(%.1f,%.1f) h=%.3g [%s] codes:", p->pos.x,
             p->pos.y, p->height, u8);
      for (int k = 0; p->text[k]; k++)
        printf(" U+%04X", p->text[k]);
      printf("\n");
      free(u8);
      if (++shown >= 40)
        break;
    }
  }
  model_free(m);
  return 0;
}