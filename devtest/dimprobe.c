/* dimprobe.c - count DIMENSION/LEADER/MULTILEADER entities and check their
   associated blocks. */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "dwgload.h"
#include <dwg.h>

static const char *nm(int t)
{
  switch (t) {
  case DWG_TYPE_DIMENSION_LINEAR: return "DIM_LINEAR";
  case DWG_TYPE_DIMENSION_ALIGNED: return "DIM_ALIGNED";
  case DWG_TYPE_DIMENSION_ANG2LN: return "DIM_ANG2LN";
  case DWG_TYPE_DIMENSION_ANG3PT: return "DIM_ANG3PT";
  case DWG_TYPE_DIMENSION_DIAMETER: return "DIM_DIAMETER";
  case DWG_TYPE_DIMENSION_RADIUS: return "DIM_RADIUS";
  case DWG_TYPE_DIMENSION_ORDINATE: return "DIM_ORDINATE";
  case DWG_TYPE_LEADER: return "LEADER";
  case DWG_TYPE_MULTILEADER: return "MULTILEADER";
  case DWG_TYPE_TOLERANCE: return "TOLERANCE";
  case DWG_TYPE_MTEXT: return "MTEXT";
  case DWG_TYPE_INSERT: return "INSERT";
  default: return "?";
  }
}

int main(int argc, char **argv)
{
  if (argc < 2) return 2;
  Dwg_Data dwg;
  memset(&dwg, 0, sizeof(dwg));
  int err = dwg_read_file(argv[1], &dwg);
  printf("read ret=%d objects=%u\n", err, dwg.num_objects);
  int counts[1024];
  memset(counts, 0, sizeof(counts));
  int dim_with_block = 0, dim_no_block = 0;
  for (unsigned int i = 0; i < dwg.num_objects; i++) {
    Dwg_Object *obj = &dwg.object[i];
    if (obj->supertype != DWG_SUPERTYPE_ENTITY || !obj->tio.entity)
      continue;
    int t = obj->fixedtype;
    if (t >= 0 && t < 1024) counts[t]++;
    if (t == DWG_TYPE_DIMENSION_LINEAR || t == DWG_TYPE_DIMENSION_ALIGNED ||
        t == DWG_TYPE_DIMENSION_ANG2LN || t == DWG_TYPE_DIMENSION_ANG3PT ||
        t == DWG_TYPE_DIMENSION_DIAMETER || t == DWG_TYPE_DIMENSION_RADIUS ||
        t == DWG_TYPE_DIMENSION_ORDINATE) {
      Dwg_DIMENSION_common *d = obj->tio.entity->tio.DIMENSION_common;
      if (d && d->block && dwg_ref_object(&dwg, d->block))
        dim_with_block++;
      else
        dim_no_block++;
    }
  }
  for (int t = 0; t < 1024; t++)
    if (counts[t] && (t >= DWG_TYPE_DIMENSION_LINEAR && t <= DWG_TYPE_DIMENSION_ORDINATE ||
                      t == DWG_TYPE_LEADER || t == DWG_TYPE_MULTILEADER ||
                      t == DWG_TYPE_TOLERANCE || t == DWG_TYPE_MTEXT))
      printf("  %-14s %d\n", nm(t), counts[t]);
  printf("dims with block=%d, without=%d\n", dim_with_block, dim_no_block);
  dwg_free(&dwg);
  return 0;
}