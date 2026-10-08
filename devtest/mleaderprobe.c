/* mleaderprobe.c - dump MULTILEADER content info. */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "dwgload.h"
#include <dwg.h>

int main(int argc, char **argv)
{
  if (argc < 2) return 2;
  Dwg_Data dwg;
  memset(&dwg, 0, sizeof(dwg));
  int err = dwg_read_file(argv[1], &dwg);
  printf("read ret=%d objects=%u\n", err, dwg.num_objects);
  int n = 0, nt = 0, nb = 0, nnull = 0;
  for (unsigned int i = 0; i < dwg.num_objects && n < 15; i++) {
    Dwg_Object *obj = &dwg.object[i];
    if (obj->supertype != DWG_SUPERTYPE_ENTITY || !obj->tio.entity)
      continue;
    if (obj->fixedtype != DWG_TYPE_MULTILEADER || !obj->tio.entity->tio.MULTILEADER)
      continue;
    Dwg_Entity_MULTILEADER *ml = obj->tio.entity->tio.MULTILEADER;
    Dwg_MLEADER_AnnotContext *c = &ml->ctx;
    printf("#%d num_leaders=%u has_txt=%d has_blk=%d text_height=%g\n", n,
           (unsigned)c->num_leaders, c->has_content_txt, c->has_content_blk,
           c->text_height);
    if (c->has_content_txt) {
      nt++;
      Dwg_MLEADER_Content_MText *t = &c->content.txt;
      printf("   TXT loc=(%g,%g) h=%g rot=%g dir=(%g,%g) text=%s\n",
             t->location.x, t->location.y, t->height, t->rotation,
             t->direction.x, t->direction.y,
             t->default_text ? t->default_text : "(null)");
      if (!t->default_text)
        nnull++;
    }
    if (c->has_content_blk) {
      nb++;
      Dwg_MLEADER_Content_Block *blk = &c->content.blk;
      printf("   BLK loc=(%g,%g) rot=%g scale=(%g,%g)\n", blk->location.x,
             blk->location.y, blk->rotation, blk->scale.x, blk->scale.y);
    }
    n++;
  }
  printf("mleaders=%d has_txt=%d has_blk=%d null_text=%d\n", n, nt, nb, nnull);
  dwg_free(&dwg);
  return 0;
}