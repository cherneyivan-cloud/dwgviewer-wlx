/* hatchdump.c - full dump of all hatch loops to stdout */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <math.h>
#include "dwgload.h"
#include <dwg.h>

int main(int argc,char**argv)
{
  if(argc<2)return 2;
  Dwg_Data dwg; memset(&dwg,0,sizeof(dwg));
  int err=dwg_read_file(argv[1],&dwg);
  fprintf(stderr,"read ret=%d objects=%u\n",err,dwg.num_objects);
  int idx=0;
  for(unsigned int i=0;i<dwg.num_objects;i++){
    Dwg_Object *obj=&dwg.object[i];
    if(obj->supertype!=DWG_SUPERTYPE_ENTITY||!obj->tio.entity)continue;
    if(obj->fixedtype!=DWG_TYPE_HATCH||!obj->tio.entity->tio.HATCH)continue;
    Dwg_Entity_HATCH *h=obj->tio.entity->tio.HATCH;
    printf("#%d paths=%lu solid=%d\n",idx,h->num_paths,h->is_solid_fill);
    for(unsigned long p=0;p<h->num_paths;p++){
      Dwg_HATCH_Path *pa=&h->paths[p];
      if(pa->flag&2){
        Dwg_HATCH_PolylinePath *pl=pa->polyline_paths;
        printf("  P%lu flag=0x%x POLY n=%lu bulges=%d\n",p,pa->flag,pa->num_segs_or_paths,pa->bulges_present);
        for(unsigned long k=0;k<pa->num_segs_or_paths;k++)
          printf("     v[%lu] (%.2f,%.2f) b=%g\n",k,pl[k].point.x,pl[k].point.y,pl[k].bulge);
      } else {
        printf("  P%lu flag=0x%x SEGS n=%lu\n",p,pa->flag,pa->num_segs_or_paths);
        for(unsigned long k=0;k<pa->num_segs_or_paths;k++){
          Dwg_HATCH_PathSeg *s=&pa->segs[k];
          if(s->curve_type==1) printf("     L (%.2f,%.2f)->(%.2f,%.2f)\n",s->first_endpoint.x,s->first_endpoint.y,s->second_endpoint.x,s->second_endpoint.y);
          else if(s->curve_type==2) printf("     A c=(%.2f,%.2f) r=%.2f a0=%.4f a1=%.4f ccw=%d\n",s->center.x,s->center.y,s->radius,s->start_angle,s->end_angle,s->is_ccw);
          else printf("     ? type=%d\n",s->curve_type);
        }
      }
    }
    idx++;
  }
  dwg_free(&dwg);
  return 0;
}
