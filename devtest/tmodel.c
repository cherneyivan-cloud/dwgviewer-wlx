/* tmodel.c - standalone test of model.c (parsing only) */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <wchar.h>
#include "model.h"

static LONG WINAPI crash_filter(EXCEPTION_POINTERS *ep)
{
  fflush(stdout);
  fprintf(stderr, "CRASH addr=%p code=0x%lx module_base=%p rva=%p\n",
          ep->ExceptionRecord->ExceptionAddress,
          ep->ExceptionRecord->ExceptionCode,
          GetModuleHandle(NULL),
          (void *)((uintptr_t)ep->ExceptionRecord->ExceptionAddress -
                   (uintptr_t)GetModuleHandle(NULL)));
  fflush(stderr);
  return EXCEPTION_CONTINUE_SEARCH;
}

int main(int argc, char **argv)
{
  SetUnhandledExceptionFilter(crash_filter);
  if (argc < 2) {
    fprintf(stderr, "usage: tmodel file.dwg\n");
    return 2;
  }
  wchar_t wpath[1024];
  MultiByteToWideChar(CP_ACP, 0, argv[1], -1, wpath, 1024);
  wchar_t errbuf[512];
  DwgModel *m = model_load(wpath, errbuf, 512);
  if (!m) {
    printf("model_load -> NULL: %ls\n", errbuf);
    return 1;
  }
  printf("version=%s objects=%u blocks=%d render_block=%d has_content=%d\n",
         m->version, m->obj_count, m->n_blocks, m->render_block,
         m->has_content);
  printf("extents x[%g..%g] y[%g..%g]\n", m->xmin, m->xmax, m->ymin, m->ymax);
  {
    Dwg_Object_Ref *msr = dwg_model_space_ref(&m->dwg);
    Dwg_Object_Ref *psr = dwg_paper_space_ref(&m->dwg);
    Dwg_Object *mso = msr ? dwg_ref_object(&m->dwg, msr) : NULL;
    Dwg_Object *pso = psr ? dwg_ref_object(&m->dwg, psr) : NULL;
    for (int i = 0; i < m->n_blocks; i++) {
      char tag[8] = "";
      if (m->blocks[i].key == (const void *)mso)
        strcpy(tag, " [MS]");
      else if (m->blocks[i].key == (const void *)pso)
        strcpy(tag, " [PS]");
      printf("  block %d: prims=%d inserts=%d%s\n", i, m->blocks[i].n_prims,
             m->blocks[i].n_inserts, tag);
    }
  }
  long long tot_poly = 0, tot_text = 0, tot_ins = 0, tot_fill = 0,
            tot_badins = 0;
  for (int i = 0; i < m->n_blocks; i++) {
    MBlock *b = &m->blocks[i];
    int np = 0, nt = 0, ni = b->n_inserts;
    int bad = 0;
    for (int j = 0; j < b->n_prims; j++) {
      if (b->prims[j].kind == KP_POLY) {
        np++;
        if (b->prims[j].fill)
          tot_fill++;
      }
      else if (b->prims[j].kind == KP_TEXT)
        nt++;
      else if (b->prims[j].kind == KP_POINT)
        np++;
      /* basic sanity: points finite */
      if (b->prims[j].n >= 2 && b->prims[j].p) {
        int k = b->prims[j].n - 1;
        if (!(b->prims[j].p[k].x > -1e18 && b->prims[j].p[k].x < 1e18))
          bad++;
      }
    }
    if (i == m->render_block)
      printf("  render block: prims=%d (poly=%d text=%d) inserts=%d bad=%d\n",
             b->n_prims, np, nt, ni, bad);
    tot_poly += np;
    tot_text += nt;
    tot_ins += ni;
    for (int k = 0; k < b->n_inserts; k++)
      if (b->inserts[k].target < 0)
        tot_badins++;
  }
  printf("totals: poly=%lld text=%lld inserts=%lld fills=%lld badins=%lld\n",
         tot_poly, tot_text, tot_ins, tot_fill, tot_badins);
  model_free(m);
  return 0;
}