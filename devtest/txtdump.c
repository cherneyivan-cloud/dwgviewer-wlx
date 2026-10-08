/* txtdump.c - MTEXT linespace / rotation probe */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <math.h>
#include "dwgload.h"
#include <dwg.h>

int main(int argc, char **argv)
{
  if (argc < 2) return 2;
  Dwg_Data dwg; memset(&dwg, 0, sizeof(dwg));
  int err = dwg_read_file(argv[1], &dwg);
  printf("read ret=%d objects=%u\n", err, dwg.num_objects);
  int n = 0;
  int hist_f[64]; memset(hist_f, 0, sizeof(hist_f));
  int hist_s[16]; memset(hist_s, 0, sizeof(hist_s));
  int rot_nonzero = 0;
  for (unsigned int i = 0; i < dwg.num_objects; i++) {
    Dwg_Object *o = &dwg.object[i];
    if (o->supertype != DWG_SUPERTYPE_ENTITY || !o->tio.entity) continue;
    Dwg_Object_Entity *e = o->tio.entity;
    if (o->fixedtype == DWG_TYPE_MTEXT && e->tio.MTEXT) {
      Dwg_Entity_MTEXT *t = e->tio.MTEXT;
      double ang = atan2(t->x_axis_dir.y, t->x_axis_dir.x) * 180.0 / M_PI;
      if (fabs(ang) > 0.5) rot_nonzero++;
      int f = (int)(t->linespace_factor * 20.0 + 0.5);
      if (f >= 0 && f < 64) hist_f[f]++;
      int s = t->linespace_style;
      if (s >= 0 && s < 16) hist_s[s]++;
      if (n < 12)
        printf("[MTEXT] style=%d factor=%g style731=%d rot=%.1f attr=%d w=%.1f h=%.1f\n",
               t->linespace_style, t->linespace_factor, t->linespace_style, ang,
               t->attachment, t->rect_width, t->text_height);
      n++;
    }
  }
  printf("MTEXT total=%d nonzero_rot=%d\n", n, rot_nonzero);
  printf("factor histogram (x20):");
  for (int i = 0; i < 64; i++) if (hist_f[i]) printf(" %.2f->%d", i/20.0, hist_f[i]);
  printf("\nstyle histogram:");
  for (int i = 0; i < 16; i++) if (hist_s[i]) printf(" %d->%d", i, hist_s[i]);
  printf("\n");
  dwg_free(&dwg);
  return 0;
}
