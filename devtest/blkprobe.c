/* blkprobe.c - find blocks containing HATCH and dump them */
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
  int shown=0;
  for(unsigned int i=0;i<dwg.num_objects && shown<6;i++){
    Dwg_Object *o=&dwg.object[i];
    if(o->supertype!=DWG_SUPERTYPE_OBJECT)continue;
    if(o->fixedtype!=DWG_TYPE_BLOCK_HEADER)continue;
    Dwg_Object_BLOCK_HEADER *bh=o->tio.object?o->tio.object->tio.BLOCK_HEADER:NULL;
    if(!bh)continue;
    Dwg_Object_Ref *cur=bh->first_entity;
    int hasH=0,cnt=0;
    while(cur){
      Dwg_Object *eo=dwg_ref_object(&dwg,cur);
      if(!eo)break;
      if(eo->supertype==DWG_SUPERTYPE_ENTITY&&eo->fixedtype==DWG_TYPE_HATCH)hasH=1;
      cnt++;
      Dwg_Object_Entity *ee=eo->tio.entity;
      if(!ee)break;
      cur=ee->next_entity;
    }
    if(!hasH)continue;
    printf("\n=== BLOCK '%s' ents=%d\n",bh->name?bh->name:"?",cnt);
    cur=bh->first_entity;
    int n2=0;
    while(cur&&n2<60){
      Dwg_Object *eo=dwg_ref_object(&dwg,cur);
      if(!eo)break;
      if(eo->supertype==DWG_SUPERTYPE_ENTITY){
        printf("   %s",eo->dxfname?eo->dxfname:"?");
        if(eo->fixedtype==DWG_TYPE_HATCH&&eo->tio.entity->tio.HATCH){
          Dwg_Entity_HATCH *h=eo->tio.entity->tio.HATCH;
          printf(" paths=%lu solid=%d",h->num_paths,h->is_solid_fill);
          for(unsigned long p=0;p<h->num_paths&&p<3;p++){
            Dwg_HATCH_Path *pa=&h->paths[p];
            if(pa->flag&2){
              Dwg_HATCH_PolylinePath *pl=pa->polyline_paths;
              printf("\n      path[%lu] f=0x%x POLY n=%lu:",p,pa->flag,pa->num_segs_or_paths);
              for(unsigned long k=0;k<pa->num_segs_or_paths&&k<16;k++)
                printf(" (%.1f,%.1f)b%g",pl[k].point.x,pl[k].point.y,pl[k].bulge);
            } else {
              printf("\n      path[%lu] f=0x%x SEGS n=%lu:",p,pa->flag,pa->num_segs_or_paths);
              for(unsigned long k=0;k<pa->num_segs_or_paths&&k<16;k++){
                Dwg_HATCH_PathSeg *s=&pa->segs[k];
                if(s->curve_type==1) printf(" L(%.1f,%.1f)->(%.1f,%.1f)",s->first_endpoint.x,s->first_endpoint.y,s->second_endpoint.x,s->second_endpoint.y);
                else if(s->curve_type==2) printf(" A(c%.1f,%.1f r%.1f a%.3f..%.3f ccw%d)",s->center.x,s->center.y,s->radius,s->start_angle,s->end_angle,s->is_ccw);
                else printf(" ?t%d",s->curve_type);
              }
            }
          }
        }
        printf("\n");
      }
      Dwg_Object_Entity *ee=eo->tio.entity;
      if(!ee)break;
      cur=ee->next_entity;
      n2++;
    }
    shown++;
  }
  dwg_free(&dwg);
  return 0;
}
