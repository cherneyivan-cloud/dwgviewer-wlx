/* hatchprobe.c - summary of HATCH loops (paths) in the model space */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <math.h>
#include "dwgload.h"
#include <dwg.h>

static void path_bbox(Dwg_HATCH_Path *pa, double *x0, double *y0, double *x1,
                      double *y1, int *npts, int *narc)
{
  *x0 = 1e300; *y0 = 1e300; *x1 = -1e300; *y1 = -1e300; *npts = 0; *narc = 0;
  if (pa->flag & 2) {
    Dwg_HATCH_PolylinePath *pl = pa->polyline_paths;
    for (unsigned long k = 0; k < pa->num_segs_or_paths; k++) {
      double x = pl[k].point.x, y = pl[k].point.y;
      if (x < *x0) *x0 = x; if (y < *y0) *y0 = y;
      if (x > *x1) *x1 = x; if (y > *y1) *y1 = y;
      (*npts)++;
      if (pl[k].bulge != 0.0) (*narc)++;
    }
  } else {
    for (unsigned long k = 0; k < pa->num_segs_or_paths; k++) {
      Dwg_HATCH_PathSeg *s = &pa->segs[k];
      double x, y;
      if (s->curve_type == 1) { x = s->first_endpoint.x; y = s->first_endpoint.y; }
      else { x = s->center.x; y = s->center.y; (*narc)++;
             if (s->radius < 0) {} }
      if (x < *x0) *x0 = x; if (y < *y0) *y0 = y;
      if (x > *x1) *x1 = x; if (y > *y1) *y1 = y;
      (*npts)++;
    }
  }
}

int main(int argc, char **argv)
{
  if (argc < 2) return 2;
  Dwg_Data dwg;
  memset(&dwg, 0, sizeof(dwg));
  int err = dwg_read_file(argv[1], &dwg);
  printf("read ret=%d objects=%u\n", err, dwg.num_objects);
  int nh = 0, multi = 0, maxp = 0;
  int hist[16];
  memset(hist, 0, sizeof(hist));
  for (unsigned int i = 0; i < dwg.num_objects; i++) {
    Dwg_Object *obj = &dwg.object[i];
    if (obj->supertype != DWG_SUPERTYPE_ENTITY || !obj->tio.entity)
      continue;
    if (obj->fixedtype != DWG_TYPE_HATCH || !obj->tio.entity->tio.HATCH)
      continue;
    Dwg_Entity_HATCH *h = obj->tio.entity->tio.HATCH;
    unsigned long np = h->num_paths;
    int b = (np < 15) ? (int)np : 15;
    hist[b]++;
    if (np > 1) multi++;
    if ((int)np > maxp) maxp = (int)np;
    if (np > 1 && nh < 40) {
      printf("HATCH paths=%lu solid=%d deflines=%lu\n", np, h->is_solid_fill,
             (unsigned long)h->num_deflines);
      for (unsigned long p = 0; p < np && p < 8; p++) {
        double x0,y0,x1,y1; int npt,narc;
        path_bbox(&h->paths[p], &x0,&y0,&x1,&y1,&npt,&narc);
        printf("   path[%lu] flag=0x%x pts=%d arcs=%d bbox=(%g,%g)-(%g,%g)\n",
               p, h->paths[p].flag, npt, narc, x0,y0,x1,y1);
      }
    }
    nh++;
  }
  printf("total hatches=%d  multi-loop=%d  max_paths=%d\n", nh, multi, maxp);
  printf("num_paths histogram:");
  for (int i = 0; i < 16; i++)
    if (hist[i]) printf("  %d->%d", i, hist[i]);
  printf("\n");
  dwg_free(&dwg);
  return 0;
}
