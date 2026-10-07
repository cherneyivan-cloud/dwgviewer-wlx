/* draw.h - GDI rendering of a DwgModel
   Part of DWG Viewer plugin for Total Commander (WLX).
   GPLv3+ */
#ifndef DWGWLX_DRAW_H
#define DWGWLX_DRAW_H

#include <windows.h>
#include "model.h"

#ifdef __cplusplus
extern "C" {
#endif

/* Affine transform: x' = a*x + c*y + e ; y' = b*x + d*y + f */
typedef struct {
  double a, b, c, d, e, f;
} XForm;

void xf_identity(XForm *t);
void xf_mul(XForm *out, const XForm *t);           /* out = t o out */
void xf_translate(XForm *t, double x, double y);
void xf_rotate(XForm *t, double rad);
void xf_scale(XForm *t, double sx, double sy);
void xf_apply(const XForm *t, double x, double y, double *ox, double *oy);
double xf_zoom(const XForm *t);                    /* approx px per model unit */

typedef struct {
  int bg_white;     /* draw on white background */
  int pen_width;    /* fixed device pen width, 0 = 1px */
} DrawOpts;

/* Render the model's selected space into hdc (client size cw x ch). */
int draw_model(HDC hdc, DwgModel *m, const XForm *view, int cw, int ch,
               const DrawOpts *o, int draw_text);

/* Fit transform that shows the whole content with margin (px), centered. */
int draw_fit_xform(DwgModel *m, double cw, double ch, double margin,
                   XForm *out);

#ifdef __cplusplus
}
#endif
#endif