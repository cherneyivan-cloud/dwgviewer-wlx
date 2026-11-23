/* textfield.c - dump TEXT width_factor and MTEXT raw codes for one DWG */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <math.h>
#include <wchar.h>
#include "dwgload.h"
#include <dwg.h>

static const char *esc(const char *s, char *out, int cap)
{
  int n = 0;
  if (!s) s = "(null)";
  for (int i = 0; s[i] && n < cap - 5; i++) {
    unsigned char c = (unsigned char)s[i];
    if (c == '\n') { out[n++] = '\\'; out[n++] = 'n'; }
    else if (c == '\\') { out[n++] = '\\'; out[n++] = '\\'; }
    else if (c >= 32 && c < 127) out[n++] = (char)c;
    else { n += sprintf(out + n, "\\x%02X", c); }
  }
  out[n] = 0;
  return out;
}

int main(int argc, char **argv)
{
  if (argc < 2) return 2;
  Dwg_Data dwg; memset(&dwg, 0, sizeof(dwg));
  int err = dwg_read_file(argv[1], &dwg);
  printf("read ret=%d objects=%u\n", err, dwg.num_objects);
  int nt = 0, nm = 0, wf_non1 = 0, braces = 0, bs = 0, bw = 0, bt = 0;
  for (unsigned int i = 0; i < dwg.num_objects; i++) {
    Dwg_Object *o = &dwg.object[i];
    if (o->supertype != DWG_SUPERTYPE_ENTITY || !o->tio.entity) continue;
    Dwg_Object_Entity *e = o->tio.entity;
    char buf[600];
    if (o->fixedtype == DWG_TYPE_TEXT && e->tio.TEXT) {
      Dwg_Entity_TEXT *t = e->tio.TEXT;
      nt++;
      if (fabs(t->width_factor - 1.0) > 1e-6) wf_non1++;
      printf("TEXT h=%.3f wf=%.3f rot=%.1f ha=%d va=%d x=%.1f y=%.1f \"%s\"\n",
             t->height, t->width_factor, t->rotation * 180.0 / M_PI,
             (int)t->horiz_alignment, (int)t->vert_alignment,
             t->ins_pt.x, t->ins_pt.y,
             esc(t->text_value, buf, sizeof(buf)));
    } else if (o->fixedtype == DWG_TYPE_MTEXT && e->tio.MTEXT) {
      Dwg_Entity_MTEXT *t = e->tio.MTEXT;
      nm++;
      if (t->text && strchr(t->text, '{')) braces++;
      if (t->text && strstr(t->text, "\\S")) bs++;
      if (t->text && strstr(t->text, "\\W")) bw++;
      if (t->text && strstr(t->text, "\\T")) bt++;
      /* hex dump of the first bytes: MTEXT text may be UTF-16 and contain
         NUL bytes, which the printable dump above cuts at */
      char hex[400];
      int hn = 0;
      if (t->text)
        for (int k = 0; k < 48 && hn < (int)sizeof(hex) - 6; k++) {
          unsigned char c = (unsigned char)t->text[k];
          hn += sprintf(hex + hn, "%02X ", c);
          if (c == 0 && k > 1 && !t->text[k + 1])
            break;
        }
      else
        strcpy(hex, "(null)");
      double swf = -1.0;
      if (t->style) {
        Dwg_Object *so = dwg_ref_object(&dwg, t->style);
        if (so && so->fixedtype == DWG_TYPE_STYLE && so->tio.object &&
            so->tio.object->tio.STYLE)
          swf = so->tio.object->tio.STYLE->width_factor;
      }
      printf("MTEXT h=%.3f rectw=%.3f att=%d lsf=%.2f stylewf=%.3f \"%s\" hex[%s]\n",
             t->text_height, t->rect_width, (int)t->attachment,
             t->linespace_factor, swf, esc(t->text, buf, sizeof(buf)), hex);
    }
  }
  printf("\nTEXT total=%d width_factor!=1: %d\n", nt, wf_non1);
  printf("MTEXT total=%d with_braces=%d with_\\S=%d with_\\W=%d with_\\T=%d\n",
         nm, braces, bs, bw, bt);
  dwg_free(&dwg);
  return 0;
}
