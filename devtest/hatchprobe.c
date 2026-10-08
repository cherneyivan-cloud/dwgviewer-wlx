/* hatchprobe.c - dump HATCH path/loop details */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <math.h>
#include "dwgload.h"
#include <dwg.h>

int main(int argc, char **argv)
{
  if (argc < 2) return 2;
  Dwg_Data dwg;
  memset(&dwg, 0, sizeof(dwg));
  int err = dwg_read_file(argv[1], &dwg);
  printf("read ret=%d objects=%u\n", err, dwg.num_objects);
  int nh = 0;
  for (unsigned int i = 0; i < dwg.num_objects; i++) {
    Dwg_Object *obj = &dwg.object[i];
    if (obj->supertype != DWG_SUPERTYPE_ENTITY || !obj->tio.entity)
      continue;
    if (obj->fixedtype != DWG_TYPE_HATCH || !obj->tio.entity->tio.HATCH)
      continue;
    Dwg_Entity_HATCH *h = obj->tio.entity->tio.HATCH;
    if (nh < 12) {
      printf("HATCH #%d solid=%d num_paths=%lu deflines=%lu double=%d\n",
             nh, h->is_solid_fill, (unsigned long)h->num_paths,
             (unsigned long)h->num_deflines, h->double_flag);
      for (unsigned long p = 0; p < h->num_paths && p < 6; p++) {
        Dwg_HATCH_Path *pa = &h->paths[p];
        printf("  path[%lu] flag=0x%x type=%s n=%lu bulges=%d polyline=%d\n",
               p, pa->flag, (pa->flag & 2) ? "polyline" : "segs",
               (unsigned long)pa->num_segs_or_paths, pa->bulges_present,
               pa->polyline_paths ? 1 : 0);
        if (pa->flag & 2) {
          Dwg_HATCH_PolylinePath *pl = pa->polyline_paths;
          for (unsigned long k = 0; k < pa->num_segs_or_paths && k < 12; k++)
            printf("      vtx[%lu] (%g,%g) bulge=%g\n", k,
                   pl[k].point.x, pl[k].point.y, pl[k].bulge);
        } else {
          Dwg_HATCH_PathSeg *sg = pa->segs;
          for (unsigned long k = 0; k < pa->num_segs_or_paths && k < 12; k++) {
            Dwg_HATCH_PathSeg *s = &sg[k];
            printf("      seg[%lu] type=%d", k, s->curve_type);
            if (s->curve_type == 1)
              printf(" (%g,%g)->(%g,%g)", s->first_endpoint.x,
                     s->first_endpoint.y, s->second_endpoint.x,
                     s->second_endpoint.y);
            else if (s->curve_type == 2)
              printf(" c=(%g,%g) r=%g a0=%g a1=%g ccw=%d", s->center.x,
                     s->center.y, s->radius, s->start_angle, s->end_angle,
                     s->is_ccw);
            else if (s->curve_type == 3)
              printf(" ell c=(%g,%g) end=(%g,%g) ratio=%g a0=%g a1=%g",
                     s->center.x, s->center.y, s->endpoint.x, s->endpoint.y,
                     s->minor_major_ratio, s->start_angle, s->end_angle);
            printf("\n");
          }
        }
      }
    }
    nh++;
  }
  printf("total hatches=%d\n", nh);
  dwg_free(&dwg);
  return 0;
}
