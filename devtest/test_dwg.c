/* Smoketest: load a DWG with LibreDWG and dump object model summary. */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <math.h>
#include <dwg.h>

static int counts[1024];

static const char *ent_name(int t)
{
  switch (t) {
  case DWG_TYPE_LINE: return "LINE";
  case DWG_TYPE_CIRCLE: return "CIRCLE";
  case DWG_TYPE_ARC: return "ARC";
  case DWG_TYPE_ELLIPSE: return "ELLIPSE";
  case DWG_TYPE_LWPOLYLINE: return "LWPOLYLINE";
  case DWG_TYPE_POLYLINE_2D: return "POLYLINE_2D";
  case DWG_TYPE_POLYLINE_3D: return "POLYLINE_3D";
  case DWG_TYPE_INSERT: return "INSERT";
  case DWG_TYPE_MINSERT: return "MINSERT";
  case DWG_TYPE_TEXT: return "TEXT";
  case DWG_TYPE_MTEXT: return "MTEXT";
  case DWG_TYPE_SPLINE: return "SPLINE";
  case DWG_TYPE_SOLID: return "SOLID";
  case DWG_TYPE__3DFACE: return "3DFACE";
  case DWG_TYPE_POINT: return "POINT";
  case DWG_TYPE_HATCH: return "HATCH";
  case DWG_TYPE_DIMENSION_ALIGNED: return "DIM_ALIGNED";
  case DWG_TYPE_DIMENSION_LINEAR: return "DIM_LINEAR";
  case DWG_TYPE_DIMENSION_RADIUS: return "DIM_RADIUS";
  case DWG_TYPE_DIMENSION_DIAMETER: return "DIM_DIAMETER";
  case DWG_TYPE_DIMENSION_ANG2LN: return "DIM_ANG2LN";
  case DWG_TYPE_DIMENSION_ANG3PT: return "DIM_ANG3PT";
  case DWG_TYPE_DIMENSION_ORDINATE: return "DIM_ORDINATE";
  case DWG_TYPE_LEADER: return "LEADER";
  case DWG_TYPE_MLINE: return "MLINE";
  case DWG_TYPE_ATTRIB: return "ATTRIB";
  case DWG_TYPE_ATTDEF: return "ATTDEF";
  case DWG_TYPE_VIEWPORT: return "VIEWPORT";
  case DWG_TYPE_IMAGE: return "IMAGE";
  case DWG_TYPE_WIPEOUT: return "WIPEOUT";
  default: return "?";
  }
}

int main(int argc, char **argv)
{
  if (argc < 2) { fprintf(stderr, "usage: test FILE.dwg\n"); return 2; }
  Dwg_Data dwg;
  memset(&dwg, 0, sizeof(dwg));
  /* reduce default log noise */
  dwg.opts = 0;
  int err = dwg_read_file(argv[1], &dwg);
  printf("dwg_read_file ret=%d\n", err);
  printf("objects=%u version=%s\n", dwg.num_objects,
         dwg_version_type(dwg.header.version));

  unsigned int total = 0;
  for (unsigned int i = 0; i < dwg.num_objects; i++) {
    Dwg_Object *obj = &dwg.object[i];
    if (obj->supertype != DWG_SUPERTYPE_ENTITY) continue;
    if (obj->fixedtype >= 0 && obj->fixedtype < 1024) {
      counts[obj->fixedtype]++;
      total++;
    }
  }
  printf("entities(total)=%u\n", total);
  for (int t = 0; t < 1024; t++)
    if (counts[t])
      printf("  %3d %-16s %d\n", t, ent_name(t), counts[t]);

  printf("model extents: x[%g..%g] y[%g..%g]\n",
         dwg_model_x_min(&dwg), dwg_model_x_max(&dwg),
         dwg_model_y_min(&dwg), dwg_model_y_max(&dwg));

  /* Look at the first INSERT: resolve its block and count block entities */
  int shown = 0;
  for (unsigned int i = 0; i < dwg.num_objects && shown < 3; i++) {
    Dwg_Object *obj = &dwg.object[i];
    if (obj->supertype != DWG_SUPERTYPE_ENTITY) continue;
    if (obj->fixedtype != DWG_TYPE_INSERT) continue;
    Dwg_Entity_INSERT *ins = obj->tio.entity->tio.INSERT;
    printf("INSERT #%u ins=(%g,%g) rot=%g scale=(%g,%g) block_header?=%s\n", i,
           ins->ins_pt.x, ins->ins_pt.y, ins->rotation, ins->scale.x,
           ins->scale.y, ins->block_header ? "yes" : "no");
    if (ins->block_header) {
      BITCODE_RL absref = ins->block_header->absolute_ref;
      Dwg_Object *hdr2 = dwg_ref_object(&dwg, ins->block_header);
      printf("  block_header absref=%llu (code=%u) dwg_ref_object->%s\n",
             (unsigned long long)absref, ins->block_header->handleref.code,
             hdr2 ? (hdr2->name ? hdr2->name : "noname") : "NULL");
      Dwg_Object *hdr = hdr2 ? hdr2
          : (absref < dwg.num_objects ? &dwg.object[absref] : NULL);
      if (hdr) {
        printf("  resolved: fixedtype=%d name=%s\n", hdr->fixedtype,
               hdr->name ? hdr->name : "?");
        if (hdr->fixedtype == DWG_TYPE_BLOCK_HEADER) {
          unsigned int bc = 0;
          Dwg_Object *e = get_first_owned_entity(hdr);
          int guard = 0;
          while (e && guard++ < 1000000) {
            bc++;
            if (e->supertype == DWG_SUPERTYPE_ENTITY)
              ; /* count */
            Dwg_Object *next = get_next_owned_entity(hdr, e);
            if (next == e) break; /* cycle guard */
            e = next;
          }
          printf("  owned entities in block: %u\n", bc);
        }
      }
    }
    shown++;
  }
  dwg_free(&dwg);
  return err < DWG_ERR_CRITICAL ? 0 : 1;
}