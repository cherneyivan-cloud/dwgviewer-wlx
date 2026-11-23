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
  double linespace;  /* KP_TEXT: MTEXT linespace_factor (1.0 = по умолчанию) */
  double wf;         /* KP_TEXT: width factor (DXF 41), 1.0 = normal */
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
  unsigned long prim_emitted;         /* count of emit calls */
  unsigned long n_texts;              /* count of text primitives */
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
  unsigned long n_texts; /* total text primitives across blocks */
  int refs;              /* reference count for the shared-model cache */
  int dwg_freed;         /* Dwg_Data already released after building */
  Dwg_Data dwg;          /* owned; freed in model_free */
} DwgModel;

/* Load a DWG file. Returns a model; on failure errbuf receives a message.
   Returns NULL only on allocation failure. */
DwgModel *model_load(const wchar_t *path, wchar_t *errbuf, int errlen);

/* Cached variant of model_load. One most-recently used model is kept alive
   for the lifetime of the process, keyed by path + size + mtime. Every
   successful call must be balanced with model_release(). */
DwgModel *model_acquire(const wchar_t *path, wchar_t *errbuf, int errlen);
void model_release(DwgModel *m);

/* Same as model_acquire, but gives up (returns NULL) if cancelled(ctx)
   returns non-zero before the parse actually starts. Used by the lister so
   that a closed window does not spend seconds parsing in the background. */
typedef int (*ModelCancelFn)(void *ctx);
DwgModel *model_acquire_ex(const wchar_t *path, wchar_t *errbuf, int errlen,
                           ModelCancelFn cancelled, void *ctx);

/* Enable/disable the shared-model cache (default: enabled). Disabling it
   keeps memory low on machines that only view each file once. */
void model_set_cache_enabled(int on);

void model_free(DwgModel *m);

/* Select which block's canvas is rendered as the "page" and recompute the
   model extents for it. Used by the viewer to switch between spaces and by
   the devtest sheet renderer. */
void model_select_block(DwgModel *m, int bi);

/* Append a timestamped diagnostic line to the plugin log (no-op if unset). */
void model_perf_log(const char *msg);

#ifdef __cplusplus
}
#endif
#endif