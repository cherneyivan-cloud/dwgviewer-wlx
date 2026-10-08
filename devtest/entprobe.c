/* entprobe.c - census of entity types; dump big ELLIPSE/ARC/CIRCLE/SPLINE */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <math.h>
#include "dwgload.h"
#include <dwg.h>

static const char *tn(Dwg_Object *o)
{
  return o->dxfname ? o->dxfname : "?";
}

int main(int argc, char **argv)
{
  if (argc < 2) return 2;
  Dwg_Data dwg;
  memset(&dwg, 0, sizeof(dwg));
  int err = dwg_read_file(argv[1], &dwg);
  printf("read ret=%d objects=%u\n", err, dwg.num_objects);
  /* count by dxfname */
  struct { char name[32]; int cnt; } cnts[256];
  int nc = 0;
  for (unsigned int i = 0; i < dwg.num_objects; i++) {
    Dwg_Object *o = &dwg.object[i];
    if (o->supertype != DWG_SUPERTYPE_ENTITY || !o->tio.entity)
      continue;
    const char *nm = tn(o);
    int found = 0;
    for (int j = 0; j < nc; j++)
      if (strcmp(cnts[j].name, nm) == 0) { cnts[j].cnt++; found = 1; break; }
    if (!found && nc < 256) {
      strncpy(cnts[nc].name, nm, 31);
      cnts[nc].cnt = 1;
      nc++;
    }
  }
  printf("--- entity type counts ---\n");
  for (int j = 0; j < nc; j++)
    if (cnts[j].cnt > 0)
      printf("  %-16s %d\n", cnts[j].name, cnts[j].cnt);

  printf("--- big ELLIPSE / ARC / CIRCLE ---\n");
  int shown = 0;
  for (unsigned int i = 0; i < dwg.num_objects && shown < 40; i++) {
    Dwg_Object *o = &dwg.object[i];
    if (o->supertype != DWG_SUPERTYPE_ENTITY || !o->tio.entity)
      continue;
    Dwg_Object_Entity *ent = o->tio.entity;
    if (o->fixedtype == DWG_TYPE_ELLIPSE && ent->tio.ELLIPSE) {
      Dwg_Entity_ELLIPSE *e = ent->tio.ELLIPSE;
      double a = sqrt(e->sm_axis.x * e->sm_axis.x + e->sm_axis.y * e->sm_axis.y);
      if (a > 200) {
        printf("  ELL c=(%g,%g) sm=(%g,%g) |a|=%g ratio=%g a0=%g a1=%g\n",
               e->center.x, e->center.y, e->sm_axis.x, e->sm_axis.y, a,
               e->axis_ratio, e->start_angle, e->end_angle);
        shown++;
      }
    } else if (o->fixedtype == DWG_TYPE_ARC && ent->tio.ARC) {
      Dwg_Entity_ARC *e = ent->tio.ARC;
      if (e->radius > 200) {
        printf("  ARC c=(%g,%g) r=%g a0=%g a1=%g\n", e->center.x, e->center.y,
               e->radius, e->start_angle, e->end_angle);
        shown++;
      }
    } else if (o->fixedtype == DWG_TYPE_CIRCLE && ent->tio.CIRCLE) {
      Dwg_Entity_CIRCLE *e = ent->tio.CIRCLE;
      if (e->radius > 200) {
        printf("  CIR c=(%g,%g) r=%g\n", e->center.x, e->center.y, e->radius);
        shown++;
      }
    }
  }
  dwg_free(&dwg);
  return 0;
}
