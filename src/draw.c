/* draw.c - GDI rendering of a DwgModel
   Part of DWG Viewer plugin for Total Commander (WLX).
   GPLv3+ */
#include <stdlib.h>
#include <stdio.h>
#include <string.h>
#include <math.h>
#include <wchar.h>

#include "draw.h"
#include "model.h"

#ifdef PROFILE_ENABLED
static unsigned long d_strokes, d_fills, d_texts, d_points, d_culled,
    d_subpx, d_batches, d_text_drawn, d_text_small, d_text_cull;
static double d_text_hmin = 1e9, d_text_hmax = 0;
#define DINC(v) ((v)++)
#else
#define DINC(v) do {} while (0)
#endif

/* ------------------------------------------------------------------ */
/* XForm                                                               */

void xf_identity(XForm *t)
{
  t->a = 1; t->b = 0; t->c = 0; t->d = 1; t->e = 0; t->f = 0;
}

void xf_mul(XForm *out, const XForm *t)
{
  /* out = t o out */
  double a = t->a * out->a + t->c * out->b;
  double b = t->b * out->a + t->d * out->b;
  double c = t->a * out->c + t->c * out->d;
  double d = t->b * out->c + t->d * out->d;
  double e = t->a * out->e + t->c * out->f + t->e;
  double f = t->b * out->e + t->d * out->f + t->f;
  out->a = a; out->b = b; out->c = c; out->d = d; out->e = e; out->f = f;
}

void xf_translate(XForm *t, double x, double y)
{
  XForm n;
  xf_identity(&n);
  n.e = x;
  n.f = y;
  xf_mul(t, &n);
}

void xf_rotate(XForm *t, double rad)
{
  XForm n;
  double c = cos(rad), s = sin(rad);
  n.a = c;   n.b = s;
  n.c = -s;  n.d = c;
  n.e = 0;   n.f = 0;
  xf_mul(t, &n);
}

void xf_scale(XForm *t, double sx, double sy)
{
  XForm n;
  xf_identity(&n);
  n.a = sx;
  n.d = sy;
  xf_mul(t, &n);
}

void xf_apply(const XForm *t, double x, double y, double *ox, double *oy)
{
  *ox = t->a * x + t->c * y + t->e;
  *oy = t->b * x + t->d * y + t->f;
}

double xf_zoom(const XForm *t)
{
  /* mean device length of the model unit vectors */
  double zx = hypot(t->a, t->b);
  double zy = hypot(t->c, t->d);
  double z = (zx + zy) / 2.0;
  return z > 0 ? z : 1.0;
}

/* ------------------------------------------------------------------ */
/* pen / brush caches                                                  */

#define PEN_CACHE_MAX 1024

typedef struct {
  DWORD key;    /* color | (style << 24) */
  HPEN pen;
  HBRUSH brush;
  int busy;
} PenEnt;

static void pen_cache_clear(PenEnt *cache, int n)
{
  for (int i = 0; i < n; i++) {
    if (cache[i].pen)
      DeleteObject(cache[i].pen);
    if (cache[i].brush)
      DeleteObject(cache[i].brush);
    cache[i].pen = NULL;
    cache[i].brush = NULL;
    cache[i].busy = 0;
  }
}

static HPEN get_pen(PenEnt *cache, COLORREF color, int style, int width)
{
  DWORD key = (DWORD)color | ((DWORD)style << 24) | ((DWORD)width << 28);
  int idx = (int)((key * 2654435761u) >> 22) & (PEN_CACHE_MAX - 1);
  int start = idx;
  do {
    if (!cache[idx].busy) {
      int ps = PS_SOLID;
      switch (style) {
      case DPS_DASH: ps = PS_DASH; break;
      case DPS_DOT: ps = PS_DOT; break;
      case DPS_DASHDOT: ps = PS_DASHDOT; break;
      case DPS_DASHDOTDOT: ps = PS_DASHDOTDOT; break;
      default: ps = PS_SOLID; break;
      }
      HPEN p = CreatePen(ps, width <= 0 ? 1 : width, color);
      cache[idx].key = key;
      cache[idx].pen = p;
      cache[idx].busy = 1;
      if (style == DPS_SOLID && width <= 1)
        cache[idx].brush = CreateSolidBrush(color);
      return p;
    }
    if (cache[idx].key == key && cache[idx].pen)
      return cache[idx].pen;
    idx = (idx + 1) & (PEN_CACHE_MAX - 1);
  } while (idx != start);
  /* full: reuse the last one */
  cache[0].busy = 0;
  if (cache[0].pen)
    DeleteObject(cache[0].pen);
  cache[0].pen = NULL;
  return get_pen(cache, color, style, width);
}

static HBRUSH get_brush(PenEnt *cache, DWORD key)
{
  /* reuse the brush from the pen cache entry, else create */
  int idx = (int)((key * 2654435761u) >> 22) & (PEN_CACHE_MAX - 1);
  int start = idx;
  do {
    if (cache[idx].brush && cache[idx].key == key)
      return cache[idx].brush;
    if (!cache[idx].busy) {
      cache[idx].brush = CreateSolidBrush((COLORREF)key);
      cache[idx].key = key;
      cache[idx].busy = 1;
      return cache[idx].brush;
    }
    idx = (idx + 1) & (PEN_CACHE_MAX - 1);
  } while (idx != start);
  return GetStockObject(WHITE_BRUSH);
}

/* ------------------------------------------------------------------ */
/* text rendering                                                      */

typedef struct {
  int h;
  int esc;
  int w; /* lfWidth: negative = requested average character width */
  HFONT font;
} TextFontEnt;

#define TEXT_FONT_CACHE 48

static void text_font_clear(TextFontEnt *c)
{
  for (int i = 0; i < TEXT_FONT_CACHE; i++) {
    if (c[i].font)
      DeleteObject(c[i].font);
    c[i].font = NULL;
  }
}

/* Creates a font for the given pixel height, escapement (1/10 degree) and
   width factor. GDI's lfWidth condenses the glyph outlines themselves, so a
   DWG width factor (DXF 41) is reproduced without a world transform - the
   latter distorts text at small sizes and does not combine well with the
   font escapement used for rotated text. The 0.448 factor was calibrated
   against measured ink widths (lfWidth = -0.448*h gives the natural width
   of Arial; smaller values compress proportionally). */
static HFONT text_font(TextFontEnt *c, int h, int esc, double wf)
{
  if (h < 2)
    h = 2;
  if (h > 3000)
    h = 3000;
  if (!(wf > 0.02) || wf > 10.0)
    wf = 1.0;
  int lw = -(int)((double)h * 0.448 * wf + 0.5);
  if (lw > -1)
    lw = -1;
  while (esc < 0)
    esc += 3600;
  esc %= 3600;
  for (int i = 0; i < TEXT_FONT_CACHE; i++) {
    if (c[i].font && c[i].h == h && c[i].esc == esc && c[i].w == lw)
      return c[i].font;
  }
  /* round-robin replacement: a single slot would thrash the cache because
     text height, escapement and width factor vary between entities */
  static int rr = 0;
  TextFontEnt *e = &c[rr++ % TEXT_FONT_CACHE];
  if (e->font)
    DeleteObject(e->font);
  e->font = CreateFontW(-h, lw, esc, 0, FW_NORMAL, FALSE, FALSE, FALSE,
                        DEFAULT_CHARSET, OUT_TT_ONLY_PRECIS,
                        CLIP_DEFAULT_PRECIS, CLEARTYPE_QUALITY,
                        DEFAULT_PITCH | FF_DONTCARE, L"Arial");
  e->h = h;
  e->esc = esc;
  e->w = lw;
  return e->font;
}

/* Цвет с учётом фона: на чёрном фоне слишком тёмные цвета делаем светлыми,
   на белом — слишком светлые делаем тёмными.  Без этого чёрный текст на
   чёрном фоне не виден (частый случай: текст чёрный, линии белые). */
static COLORREF adjust_color(COLORREF c, int bg_white)
{
  int r = GetRValue(c), g = GetGValue(c), b = GetBValue(c);
  int mx = r > g ? (r > b ? r : b) : (g > b ? g : b);
  int mn = r < g ? (r < b ? r : b) : (g < b ? g : b);
  if (!bg_white) {
    /* на чёрном фоне невидимы только почти-чёрные цвета (делаем белыми);
       насыщенные (синий/зелёный/красный) сохраняем */
    if (mx < 24)
      return RGB(255, 255, 255);
  } else {
    /* на белом фоне почти-белые делаем чёрными */
    if (mn > 232)
      return RGB(0, 0, 0);
  }
  return c;
}

/* starts/ends: indices into text string per line */

/* When the drawing contains many text entities (large maps/plans), drawing
   every label at the 8 px readability floor turns the overview into an
   unreadable smear and dominates paint time. In that case labels are drawn
   only at their true size and skipped when below ~3 px; zooming in brings
   them back. Small drawings keep the old "always readable" floor. */
static double g_text_min_px = 4.0;
static int g_text_cull_margin = 1024;
/* Multiplier on top of the DWG width factor (1.0 = exactly as in the file).
   Some fonts substitute differently from AutoCAD's SHX fonts; this lets the
   user soften or strengthen the condensation without a rebuild. */
static double g_text_wf_scale = 1.0;

void draw_set_text_min_px(double px)
{
  if (px < 0.0)
    px = 0.0;
  if (px > 64.0)
    px = 64.0;
  g_text_min_px = px;
}

void draw_set_text_width_scale(double scale)
{
  if (!(scale > 0.2))
    scale = 1.0;
  if (scale > 3.0)
    scale = 3.0;
  g_text_wf_scale = scale;
}

static void draw_text_prim(HDC hdc, KPrim *p, const XForm *xf,
                           PenEnt *pc, TextFontEnt *fc, int draw_text,
                           int bg_white, int cw, int ch)
{
  (void)pc;
  if (!draw_text || !p->text || !p->text[0])
    return;
  /* device properties: height and reading direction */
  double ax, ay;
  xf_apply(xf, 0.0, 0.0, &ax, &ay);
  double ux, uy, vx, vy;
  xf_apply(xf, 1.0, 0.0, &ux, &uy);
  xf_apply(xf, 0.0, 1.0, &vx, &vy);
  double dirx = ux - ax, diry = uy - ay; /* ось X модели в экране */
  double upx = vx - ax, upy = vy - ay;   /* ось Y модели в экране */
  double hpx = hypot(upx, upy) * p->height;
  /* направление базовой линии текста с учётом поворота p->rot */
  double cb = cos(p->rot), sb = sin(p->rot);
  double bx2, by2, dx2, dy2;
  xf_apply(xf, cb, sb, &bx2, &by2);
  double bdx = bx2 - ax, bdy = by2 - ay;
  double blen = hypot(bdx, bdy);
  if (blen < 1e-12) { bdx = dirx; bdy = diry; blen = hypot(bdx, bdy); }
  if (blen < 1e-12) { bdx = 1; bdy = 0; blen = 1; }
  double ubx = bdx / blen, uby = bdy / blen; /* единичный вектор по строке */
  /* направление "вниз" строки (перпендикуляр к базовой линии) */
  double dnx = sb, dny = -cb;
  xf_apply(xf, dnx, dny, &dx2, &dy2);
  double ddx = dx2 - ax, ddy = dy2 - ay;
  double dlen = hypot(ddx, ddy);
  if (dlen < 1e-12) { ddx = 0; ddy = 1; dlen = 1; }
  double udx = ddx / dlen, udy = ddy / dlen; /* единичный вектор вниз */
  /* Text is never enlarged beyond its true size: it appears when it becomes
     big enough to read at the current zoom, so at the overview it cannot
     cover the geometry. g_text_min_px is the smallest size we bother with. */
  if (hpx < g_text_min_px) {
    DINC(d_text_small);
    return;
  }
  if (hpx > 3000)
    hpx = 3000;
#ifdef PROFILE_ENABLED
  DINC(d_text_drawn);
  if (hpx < d_text_hmin)
    d_text_hmin = hpx;
  if (hpx > d_text_hmax)
    d_text_hmax = hpx;
#endif
  /* GDI-угол (escapement) отсчитывается против часовой в системе с осью Y
     вверх, а наши экранные координаты — с Y вниз, поэтому знак Y инвертируем:
     иначе повёрнутый текст выходит развёрнутым на 180°. */
  int esc = (int)floor(atan2(-bdy, bdx) * (180.0 / M_PI) * 10.0 + 0.5);
  double wf = p->wf * g_text_wf_scale;
  if (wf < 0.05)
    wf = 0.05;
  if (wf > 10.0)
    wf = 10.0;

  double px0, py0;
  xf_apply(xf, p->pos.x, p->pos.y, &px0, &py0);
  /* cheap viewport cull: skip labels whose anchor is far outside the window */
  if (px0 < -g_text_cull_margin || px0 > cw + g_text_cull_margin ||
      py0 < -g_text_cull_margin || py0 > ch + g_text_cull_margin) {
    DINC(d_text_cull);
    return;
  }

  HFONT font = text_font(fc, (int)(hpx + 0.5), esc, wf);
  HFONT old = (HFONT)SelectObject(hdc, font);
  int oldta = SetTextAlign(hdc, TA_LEFT | TA_BASELINE | TA_NOUPDATECP);
  SetBkMode(hdc, TRANSPARENT);
  SetTextColor(hdc, adjust_color(p->color, bg_white));

  /* Разбиваем текст на строки (с переносом по ширине рамки), сохраняя ссылки
     внутрь p->text (перенос не вставляет символов). Затем выводим с учётом
     точки привязки MTEXT (attachment 1..9). */
  double wscale = hypot(dirx, diry);
  if (wscale < 1e-9)
    wscale = 1.0;
  double maxpx = (p->wrap_width > 0) ? p->wrap_width * wscale : 0.0;

  const wchar_t *line_p[1024];
  int line_l[1024];
  int nlines = 0;
  wchar_t buf[4096];
  const wchar_t *s = p->text;
  while (s && *s && nlines < 1024) {
    const wchar_t *nl = wcschr(s, L'\n');
    int plen = nl ? (int)(nl - s) : (int)wcslen(s);
    if (plen < 0)
      plen = 0;
    if (maxpx <= 1.0) {
      line_p[nlines] = s;
      line_l[nlines] = plen;
      nlines++;
    } else {
      int i = 0;
      while (i < plen && nlines < 1024) {
        int count = 0, k = i;
        while (k < plen) {
          int wend = k;
          while (wend < plen && s[wend] != L' ')
            wend++;
          int cand = wend - i;
          if (cand > 4090)
            cand = 4090;
          memcpy(buf, s + i, (size_t)cand * sizeof(wchar_t));
          buf[cand] = 0;
          SIZE sz;
          GetTextExtentPoint32W(hdc, buf, cand, &sz);
          if ((double)sz.cx <= maxpx) {
            count = cand;
            k = (wend < plen) ? wend + 1 : wend;
            if (wend >= plen)
              break;
          } else {
            break;
          }
        }
        if (count == 0) { /* слово шире рамки — выводим его целиком */
          int wend = i;
          while (wend < plen && s[wend] != L' ')
            wend++;
          count = wend - i;
          if (count == 0)
            count = 1;
        }
        int drawLen = count;
        while (drawLen > 0 && s[i + drawLen - 1] == L' ')
          drawLen--;
        line_p[nlines] = s + i;
        line_l[nlines] = drawLen;
        nlines++;
        i += count;
        while (i < plen && s[i] == L' ')
          i++;
      }
    }
    s = nl ? nl + 1 : NULL;
  }
  if (nlines == 0) {
    SetTextAlign(hdc, oldta);
    SelectObject(hdc, old);
    return;
  }

  /* Интерлиньяж MTEXT: базовый = 5/3·высоты (значение DWG «3-on-5»),
     домножается на linespace_factor (DXF 44). */
  double lsf = (p->linespace > 0.25 && p->linespace < 4.0) ? p->linespace : 1.0;
  double lsp = hpx * (5.0 / 3.0) * lsf;
  int at = p->attach;
  int ha = 0;
  double bx0 = px0, by0 = py0; /* базовая точка строки 0 (слева) */
  if (at >= 1 && at <= 9) {
    int va = (at - 1) / 3; /* 0 top, 1 middle, 2 bottom */
    ha = (at - 1) % 3;     /* 0 left, 1 center, 2 right */
    TEXTMETRICW tm;
    double asc = hpx, desc = 0;
    if (GetTextMetricsW(hdc, &tm)) {
      asc = tm.tmAscent;
      desc = tm.tmDescent;
    }
    double off; /* смещение базовой линии строки 0 вдоль «вниз» */
    if (va == 0)
      off = asc;                                                    /* top */
    else if (va == 1)
      off = -((double)(nlines - 1) * lsp + desc - asc) / 2.0;       /* middle */
    else
      off = -((double)(nlines - 1) * lsp + desc);                   /* bottom */
    bx0 = px0 + udx * off;
    by0 = py0 + udy * off;
  }

  /* Width factor (DXF 41). GDI has no per-font horizontal scale switch, so a
     world transform on the DC scales the glyphs. When a width factor is
     active the rotation is baked into that same transform and the font's
     escapement is left at zero: combining escapement with a world transform
     made GDI distort rotated glyphs (vertical table text fell apart). */
  double line_off[1024];
  for (int li = 0; li < nlines; li++) {
    double off = 0.0;
    if (ha == 1 || ha == 2) {
      SIZE sz;
      GetTextExtentPoint32W(hdc, line_p[li], line_l[li], &sz);
      off = (ha == 1) ? sz.cx / 2.0 : (double)sz.cx;
    }
    line_off[li] = off;
  }

  for (int li = 0; li < nlines; li++) {
    double lx = bx0 + udx * ((double)li * lsp) - ubx * line_off[li];
    double ly = by0 + udy * ((double)li * lsp) - uby * line_off[li];
    if (line_l[li] > 0)
      TextOutW(hdc, (int)(lx + 0.5), (int)(ly + 0.5), line_p[li],
               line_l[li]);
  }
  SetTextAlign(hdc, oldta);
  SelectObject(hdc, old);
}

static int dev_in_range(double x, double y)
{
  /* GDI coordinates are ints; casting absurd values is UB and produces
     random lines. Such primitives are always off-screen anyway. */
  return isfinite(x) && isfinite(y) && fabs(x) < 1e9 && fabs(y) < 1e9;
}

/* transform points to device space once and report the device bbox */
static int prim_to_dev_box(KPrim *p, const XForm *xf, POINT *dev, int max,
                           int *minx, int *miny, int *maxx, int *maxy)
{
  int n = 0;
  if (p->kind == KP_POINT) {
    if (max < 1)
      return 0;
    double x, y;
    xf_apply(xf, p->p[0].x, p->p[0].y, &x, &y);
    if (!dev_in_range(x, y))
      return 0;
    dev[0].x = (int)(x + 0.5);
    dev[0].y = (int)(y + 0.5);
    n = 1;
  } else if (p->kind == KP_POLY) {
    n = p->n > max ? max : p->n;
    for (int i = 0; i < n; i++) {
      double x, y;
      xf_apply(xf, p->p[i].x, p->p[i].y, &x, &y);
      if (!dev_in_range(x, y))
        return 0;
      dev[i].x = (int)(x + 0.5);
      dev[i].y = (int)(y + 0.5);
    }
  } else {
    return 0;
  }
  if (n < 1)
    return 0;
  int mnx = dev[0].x, mny = dev[0].y, mxx = dev[0].x, mxy = dev[0].y;
  for (int i = 1; i < n; i++) {
    if (dev[i].x < mnx) mnx = dev[i].x;
    if (dev[i].x > mxx) mxx = dev[i].x;
    if (dev[i].y < mny) mny = dev[i].y;
    if (dev[i].y > mxy) mxy = dev[i].y;
  }
  *minx = mnx; *miny = mny; *maxx = mxx; *maxy = mxy;
  return n;
}

/* ------------------------------------------------------------------ */
/* main drawing                                                        */

/* Stroke batching: consecutive polylines that share one GDI pen are
   accumulated and submitted with a single PolyPolyline call. Draw order
   is preserved exactly (a batch is flushed as soon as the pen changes),
   so rendering is byte-identical to one Polyline per primitive, but GDI
   call overhead drops dramatically on drawings with many entities. */

typedef struct {
  POINT *pts;
  DWORD *cnt;
  int n_poly, cap_poly;
  int n_pt, cap_pt;
  HPEN pen;
} StrokeBatch;

#define BATCH_MAX_PTS 262144

static void batch_flush(HDC hdc, StrokeBatch *b)
{
  if (b->n_poly > 0) {
    PolyPolyline(hdc, b->pts, b->cnt, (DWORD)b->n_poly);
    DINC(d_batches);
  }
  b->n_poly = 0;
  b->n_pt = 0;
}

static void batch_append(HDC hdc, StrokeBatch *b, const POINT *dev, int n)
{
  if (b->n_poly >= b->cap_poly) {
    int nc = b->cap_poly ? b->cap_poly * 2 : 256;
    DWORD *np = (DWORD *)realloc(b->cnt, (size_t)nc * sizeof(DWORD));
    if (!np)
      return;
    b->cnt = np;
    b->cap_poly = nc;
  }
  if (b->n_pt + n > b->cap_pt) {
    int nc = b->cap_pt ? b->cap_pt * 2 : 8192;
    while (nc < b->n_pt + n)
      nc *= 2;
    POINT *np = (POINT *)realloc(b->pts, (size_t)nc * sizeof(POINT));
    if (!np)
      return;
    b->pts = np;
    b->cap_pt = nc;
  }
  memcpy(&b->pts[b->n_pt], dev, (size_t)n * sizeof(POINT));
  b->cnt[b->n_poly++] = (DWORD)n;
  b->n_pt += n;
  if (b->n_pt >= BATCH_MAX_PTS)
    batch_flush(hdc, b);
}

static void batch_free(StrokeBatch *b)
{
  free(b->pts);
  free(b->cnt);
  memset(b, 0, sizeof(*b));
}

static void draw_block_rec(HDC hdc, DwgModel *m, int bi, const XForm *parent,
                           const DrawOpts *o, int depth, int cw, int ch,
                           int draw_text, PenEnt *pc, TextFontEnt *fc,
                           HPEN *cur_pen, HBRUSH *cur_brush,
                           StrokeBatch *bat);

static void draw_prim(HDC hdc, KPrim *p, const XForm *xf, const DrawOpts *o,
                      int cw, int ch, int draw_text, PenEnt *pc,
                      TextFontEnt *fc, HPEN *cur_pen, HBRUSH *cur_brush,
                      StrokeBatch *bat)
{
  if (p->kind == KP_TEXT) {
    DINC(d_texts);
    batch_flush(hdc, bat);
    bat->pen = NULL;
    draw_text_prim(hdc, p, xf, pc, fc, draw_text, o->bg_white, cw, ch);
    return;
  }

  POINT dev[2048];
  int minx, miny, maxx, maxy;
  int n = prim_to_dev_box(p, xf, dev, 2048, &minx, &miny, &maxx, &maxy);
  if (n < 1)
    return;

  /* viewport cull */
  const int margin = 8;
  if (maxx < -margin || minx > cw + margin || maxy < -margin ||
      miny > ch + margin) {
    DINC(d_culled);
    return;
  }

  if (p->kind == KP_POINT) {
    DINC(d_points);
    int x = dev[0].x, y = dev[0].y;
    const int s = 3;
    batch_flush(hdc, bat);
    bat->pen = NULL;
    COLORREF col = adjust_color(p->color, o->bg_white);
    HPEN pen = get_pen(pc, col, DPS_SOLID, o->pen_width);
    if (*cur_pen != pen) {
      SelectObject(hdc, pen);
      *cur_pen = pen;
    }
    MoveToEx(hdc, x - s, y, NULL);
    LineTo(hdc, x + s, y);
    MoveToEx(hdc, x, y - s, NULL);
    LineTo(hdc, x, y + s);
    return;
  }

  if (n < 2)
    return;

  /* Sub-pixel stroke: both extents round to a single device pixel, so the
     stroke is invisible at this zoom. Skipping it saves a lot of GDI work
     when a large drawing is fitted into a small window. */
  if (!p->fill && o->pen_width <= 1 && maxx == minx && maxy == miny) {
    DINC(d_subpx);
    return;
  }

  COLORREF col = adjust_color(p->color, o->bg_white);
  if (p->fill) {
    DINC(d_fills);
    batch_flush(hdc, bat);
    bat->pen = NULL;
    HPEN pen = get_pen(pc, col, p->pen, o->pen_width);
    if (*cur_pen != pen) {
      SelectObject(hdc, pen);
      *cur_pen = pen;
    }
    HBRUSH br;
    int del = 0;
    if (p->fill == 1) {
      br = get_brush(pc, (DWORD)col);
    } else {
      int st;
      switch (p->fill) {
      case 2: st = HS_BDIAGONAL; break;
      case 3: st = HS_FDIAGONAL; break;
      case 4: st = HS_HORIZONTAL; break;
      case 5: st = HS_VERTICAL; break;
      case 6: st = HS_DIAGCROSS; break;
      default: st = HS_BDIAGONAL; break;
      }
      br = CreateHatchBrush(st, col);
      del = 1;
      /* фон штрихового браша = фон окна, чтобы не закрашивать промежутки */
      SetBkColor(hdc, o->bg_white ? RGB(255, 255, 255) : RGB(0, 0, 0));
    }
    HBRUSH oldbr;
    if (del) {
      oldbr = (HBRUSH)SelectObject(hdc, br);
    } else if (*cur_brush != br) {
      oldbr = (HBRUSH)SelectObject(hdc, br);
      *cur_brush = br;
    } else {
      oldbr = br;
    }
    /* WINDING вместо ALTERNATE: убирает ложные «дыры» внутри контура при
       самопересечении петель (частые артефакты штриховок) */
    int oldmode = SetPolyFillMode(hdc, WINDING);
    Polygon(hdc, dev, n);
    SetPolyFillMode(hdc, oldmode);
    if (del) {
      SelectObject(hdc, oldbr);
      DeleteObject(br);
      *cur_brush = NULL;
    }
    return;
  }

  /* stroke: extend the current batch, switching pens only between batches */
  DINC(d_strokes);
  HPEN pen = get_pen(pc, col, p->pen, o->pen_width);
  if (bat->pen != pen) {
    batch_flush(hdc, bat); /* draw the previous run with its own pen */
    if (*cur_pen != pen) {
      SelectObject(hdc, pen);
      *cur_pen = pen;
    }
    bat->pen = pen;
  }
  batch_append(hdc, bat, dev, n);
}

static void draw_block_rec(HDC hdc, DwgModel *m, int bi, const XForm *parent,
                           const DrawOpts *o, int depth, int cw, int ch,
                           int draw_text, PenEnt *pc, TextFontEnt *fc,
                           HPEN *cur_pen, HBRUSH *cur_brush,
                           StrokeBatch *bat)
{
  if (bi < 0 || bi >= m->n_blocks || depth > 48)
    return;
  MBlock *b = &m->blocks[bi];
  for (int i = 0; i < b->n_prims; i++)
    draw_prim(hdc, &b->prims[i], parent, o, cw, ch, draw_text, pc, fc,
              cur_pen, cur_brush, bat);
  for (int i = 0; i < b->n_inserts; i++) {
    MInsert *ir = &b->inserts[i];
    if (ir->target < 0)
      continue;
    if (fabs(ir->scale[0]) > 1e12 || fabs(ir->scale[1]) > 1e12)
      continue;
    XForm t;
    xf_identity(&t);
    xf_scale(&t, ir->scale[0], ir->scale[1]); /* применяется к точке блока */
    xf_rotate(&t, ir->rot);
    xf_translate(&t, ir->pos.x, ir->pos.y);
    xf_mul(&t, parent);                        /* t = parent ∘ (T∘R∘S) */
    draw_block_rec(hdc, m, ir->target, &t, o, depth + 1, cw, ch, draw_text,
                   pc, fc, cur_pen, cur_brush, bat);
  }
}

static void draw_extent_frame(HDC hdc, DwgModel *m, const XForm *view,
                              int cw, int ch, int bg_white)
{
  (void)cw;
  (void)ch;
  if (!m->has_content)
    return;
  POINT pts[5];
  double ax, ay;
  xf_apply(view, m->xmin, m->ymin, &ax, &ay);
  pts[0].x = (int)(ax + 0.5); pts[0].y = (int)(ay + 0.5);
  xf_apply(view, m->xmax, m->ymin, &ax, &ay);
  pts[1].x = (int)(ax + 0.5); pts[1].y = (int)(ay + 0.5);
  xf_apply(view, m->xmax, m->ymax, &ax, &ay);
  pts[2].x = (int)(ax + 0.5); pts[2].y = (int)(ay + 0.5);
  xf_apply(view, m->xmin, m->ymax, &ax, &ay);
  pts[3].x = (int)(ax + 0.5); pts[3].y = (int)(ay + 0.5);
  pts[4] = pts[0];
  HPEN pen = CreatePen(PS_DOT, 1, bg_white ? RGB(160, 160, 160)
                                           : RGB(80, 80, 80));
  HPEN old = (HPEN)SelectObject(hdc, pen);
  SetBkMode(hdc, TRANSPARENT);
  Polyline(hdc, pts, 5);
  SelectObject(hdc, old);
  DeleteObject(pen);
}

int draw_model(HDC hdc, DwgModel *m, const XForm *view, int cw, int ch,
               const DrawOpts *o, int draw_text)
{
  if (!m)
    return 0;
  ULONGLONG t0 = GetTickCount64();
  RECT rc = { 0, 0, cw, ch };
  COLORREF bg = o->bg_white ? RGB(255, 255, 255) : RGB(0, 0, 0);
  HBRUSH bgb = CreateSolidBrush(bg);
  FillRect(hdc, &rc, bgb);
  DeleteObject(bgb);

  PenEnt pens[PEN_CACHE_MAX];
  memset(pens, 0, sizeof(pens));
  TextFontEnt fonts[TEXT_FONT_CACHE];
  memset(fonts, 0, sizeof(fonts));

  int saved = SaveDC(hdc);
  IntersectClipRect(hdc, 0, 0, cw, ch);
  HPEN cur_pen = NULL;
  HBRUSH cur_brush = NULL;
  StrokeBatch bat;
  memset(&bat, 0, sizeof(bat));
  draw_block_rec(hdc, m, m->render_block, view, o, 0, cw, ch, draw_text,
                 pens, fonts, &cur_pen, &cur_brush, &bat);
  batch_flush(hdc, &bat);
  draw_extent_frame(hdc, m, view, cw, ch, o->bg_white);
  RestoreDC(hdc, saved);
  batch_free(&bat);

  pen_cache_clear(pens, PEN_CACHE_MAX);
  text_font_clear(fonts);

#ifdef PROFILE_ENABLED
  {
    char buf[200];
    snprintf(buf, sizeof(buf),
             "[PERF] draw_model %dx%d: delta %llu ms", cw, ch,
             (unsigned long long)(GetTickCount64() - t0));
    model_perf_log(buf);
    snprintf(buf, sizeof(buf),
             "[STAT] draw prims: strokes=%lu fills=%lu texts=%lu points=%lu"
             " culled=%lu subpx=%lu batches=%lu",
             d_strokes, d_fills, d_texts, d_points, d_culled, d_subpx,
             d_batches);
    model_perf_log(buf);
    snprintf(buf, sizeof(buf),
             "[STAT] text: drawn=%lu small=%lu culled=%lu minpx=%.1f"
             " hpx=[%.1f..%.1f] limit=%.1f",
             d_text_drawn, d_text_small, d_text_cull,
             d_text_drawn ? d_text_hmin : 0.0, d_text_hmin, d_text_hmax,
             g_text_min_px);
    model_perf_log(buf);
    d_strokes = d_fills = d_texts = d_points = d_culled = d_subpx =
        d_batches = 0;
    d_text_drawn = d_text_small = d_text_cull = 0;
    d_text_hmin = 1e9;
    d_text_hmax = 0;
  }
#else
  (void)t0;
#endif
  return 1;
}

int draw_fit_xform(DwgModel *m, double cw, double ch, double margin,
                   XForm *out)
{
  xf_identity(out);
  if (!m || !m->has_content)
    return 0;
  double exw = m->xmax - m->xmin;
  double eyh = m->ymax - m->ymin;
  if (exw <= 0)
    exw = 1;
  if (eyh <= 0)
    eyh = 1;
  if (exw > 1e15 || eyh > 1e15)
    return 0;
  double s = (cw - 2 * margin) / exw;
  double sy = (ch - 2 * margin) / eyh;
  if (sy < s)
    s = sy;
  if (s <= 0)
    s = 1e-6;
  /* center the content, flipping y */
  out->a = s;
  out->b = 0;
  out->c = 0;
  out->d = -s;
  out->e = (cw - (m->xmin + m->xmax) * s) / 2.0;
  out->f = (ch + (m->ymin + m->ymax) * s) / 2.0;
  return 1;
}