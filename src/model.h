/* model.h - parsed DWG rendering model
   Part of DWG Viewer plugin for Total Commander (WLX).
   GPLv3+ */
#ifndef DWGWLX_MODEL_H
#define DWGWLX_MODEL_H

#include <windows.h>
#include <dwg.h>

#ifdef __cplusplus
extern "C" {
#endif

typedef struct { double x, y; } MPt;

/* line styles */
enum {
  DPS_SOLID = 0,
  DPS_DASH = 1,
  DPS_DOT = 2,
  DPS_DASHDOT = 3,
  DPS_DASHDOTDOT = 4
};

typedef enum { KP_POLY, KP_TEXT, KP_POINT } KPrimKind;

typedef struct {
  KPrimKind kind;
  int fill;          /* KP_POLY: 1 = filled polygon */
  int pen;           /* KP_POLY: DPS_* */
  COLORREF color;
  /* KP_POLY / KP_POINT */
  MPt *p;            /* poly points (n >= 2) or point position (n == 1) */
  int n;
  /* KP_TEXT */
  MPt pos;
  double height;     /* model units */
  double rot;        /* radians */
  double wrap_width; /* KP_TEXT: ширина рамки в ед. модели (0 = без переноса) */
  int attach;        /* KP_TEXT: MTEXT attachment 1..9 (0 = baseline, left) */
  wchar_t *text;     /* may contain '\n' line breaks */
} KPrim;

typedef struct {
  MPt pos;           /* insertion point */
  double scale[2];
  double rot;        /* radians */
  int target;        /* index into model->blocks, or -1 if unresolvable */
} MInsert;

typedef struct {
  int id;
  const void *key;                    /* Dwg_Object * identity */
  KPrim *prims;      int n_prims, cap_prims;
  MInsert *inserts;  int n_inserts, cap_inserts;
} MBlock;

typedef struct {
  int n_blocks;
  MBlock *blocks;
  int render_block;      /* index of space to render, -1 if empty */
  /* extents of the rendered content (model units) */
  double xmin, ymin, xmax, ymax;
  int has_content;
  char version[32];
  unsigned obj_count;
  Dwg_Data dwg;          /* owned; freed in model_free */
} DwgModel;

/* Load a DWG file. Returns a model; on failure errbuf receives a message.
   Returns NULL only on allocation failure. */
DwgModel *model_load(const wchar_t *path, wchar_t *errbuf, int errlen);

void model_free(DwgModel *m);

#ifdef __cplusplus
}
#endif
#endif