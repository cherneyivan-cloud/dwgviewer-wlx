/* hatchprobe.c - compact dump of ALL hatch loops */
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
  printf("read ret=%d objects=%u\n",err,dwg.num_objects);
  int idx=0;
  for(unsigned int i=0;i<dwg.num_objects;i++){
    Dwg_Object *obj=&dwg.object[i];
    if(obj->supertype!=DWG_SUPERTYPE_ENTITY||!obj->tio.entity)continue;
    if(obj->fixedtype!=DWG_TYPE_HATCH||!obj->tio.entity->tio.HATCH)continue;
    Dwg_Entity_HATCH *h=obj->tio.entity->tio.HATCH;
    printf("#%d paths=%lu solid=%d",idx,h->num_paths,h->is_solid_fill);
    for(unsigned long p=0;p<h->num_paths&&p<4;p++){
      Dwg_HATCH_Path *pa=&h->paths[p];
      if(pa->flag&2){
        Dwg_HATCH_PolylinePath *pl=pa->polyline_paths;
        printf("  [poly n=%lu:",pa->num_segs_or_paths);
        for(unsigned long k=0;k<pa->num_segs_or_paths;k++)
          printf(" %c",pl[k].bulge!=0?'A':'L');
        printf("]");
      } else {
        printf("  [segs n=%lu:",pa->num_segs_or_paths);
        for(unsigned long k=0;k<pa->num_segs_or_paths;k++)
          printf(" %d",pa->segs[k].curve_type);
        printf("]");
      }
    }
    printf("\n");
    idx++;
  }
  printf("total=%d\n",idx);
  dwg_free(&dwg);
  return 0;
}
