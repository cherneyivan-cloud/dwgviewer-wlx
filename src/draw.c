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

static HFONT text_font(TextFontEnt *c, int h, int esc)
{
  if (h < 2)
    h = 2;
  if (h > 3000)
    h = 3000;
  while (esc < 0)
    esc += 3600;
  esc %= 3600;
  for (int i = 0; i < TEXT_FONT_CACHE; i++) {
    if (c[i].font && c[i].h == h && c[i].esc == esc)
      return c[i].font;
  }
  TextFontEnt *e = &c[0];
  if (e->font)
    DeleteObject(e->font);
  e->font = CreateFontW(-h, 0, esc, 0, FW_NORMAL, FALSE, FALSE, FALSE,
                        DEFAULT_CHARSET, OUT_TT_ONLY_PRECIS,
                        CLIP_DEFAULT_PRECIS, CLEARTYPE_QUALITY,
                        DEFAULT_PITCH | FF_DONTCARE, L"Arial");
  e->h = h;
  e->esc = esc;
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
static void draw_text_prim(HDC hdc, KPrim *p, const XForm *xf,
                           PenEnt *pc, TextFontEnt *fc, int draw_text,
                           int bg_white)
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
  /* Минимальный читаемый размер шрифта.  На чертежах с большим охватом
     (например, 10^6..10^7 единиц) реальная высота текста в пикселях при
     подгонке под окно оказывается < 1 px, и тайтлы пропадают.  Прижимаем
     снизу, чтобы подписи всегда были читаемы (как у карт/навигаторов). */
  if (hpx < 8.0)
    hpx = 8.0;
  if (hpx > 3000)
    hpx = 3000;
  int esc = (int)floor(atan2(bdy, bdx) * (180.0 / M_PI) * 10.0 + 0.5);
  HFONT font = text_font(fc, (int)(hpx + 0.5), esc);
  HFONT old = (HFONT)SelectObject(hdc, font);
  int oldta = SetTextAlign(hdc, TA_LEFT | TA_BASELINE | TA_NOUPDATECP);
  SetBkMode(hdc, TRANSPARENT);
  SetTextColor(hdc, adjust_color(p->color, bg_white));

  /* Разбиваем текст на строки (с переносом по ширине рамки), сохраняя ссылки
     внутрь p->text (перенос не вставляет символов). Затем выводим с учётом
     точки привязки MTEXT (attachment 1..9). */
  double px0, py0;
  xf_apply(xf, p->pos.x, p->pos.y, &px0, &py0);
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

  for (int li = 0; li < nlines; li++) {
    double off = 0.0;
    if (ha == 1 || ha == 2) {
      SIZE sz;
      GetTextExtentPoint32W(hdc, line_p[li], line_l[li], &sz);
      off = (ha == 1) ? sz.cx / 2.0 : (double)sz.cx;
    }
    double lx = bx0 + udx * ((double)li * lsp) - ubx * off;
    double ly = by0 + udy * ((double)li * lsp) - uby * off;
    if (line_l[li] > 0)
      TextOutW(hdc, (int)(lx + 0.5), (int)(ly + 0.5), line_p[li],
               line_l[li]);
  }
  SetTextAlign(hdc, oldta);
  SelectObject(hdc, old);
}

/* convert a prim to device polyline points */
static int prim_to_dev(KPrim *p, const XForm *xf, POINT *dev, int max)
{
  if (p->kind == KP_POINT) {
    if (max < 1)
      return 0;
    double x, y;
    xf_apply(xf, p->p[0].x, p->p[0].y, &x, &y);
    dev[0].x = (int)(x + 0.5);
    dev[0].y = (int)(y + 0.5);
    return 1;
  }
  if (p->kind != KP_POLY)
    return 0;
  int n = p->n > max ? max : p->n;
  for (int i = 0; i < n; i++) {
    double x, y;
    xf_apply(xf, p->p[i].x, p->p[i].y, &x, &y);
    dev[i].x = (int)(x + 0.5);
    dev[i].y = (int)(y + 0.5);
  }
  return n;
}

static int prim_visible(KPrim *p, const XForm *xf, int cw, int ch)
{
  int margin = 8;
  double minx = 1e300, miny = 1e300, maxx = -1e300, maxy = -1e300;
  int np = p->n;
  if (p->kind == KP_POINT)
    np = 1;
  else if (p->kind != KP_POLY)
    return 1;
  for (int i = 0; i < np; i++) {
    double x, y;
    xf_apply(xf, p->p[i].x, p->p[i].y, &x, &y);
    if (x < minx) minx = x;
    if (x > maxx) maxx = x;
    if (y < miny) miny = y;
    if (y > maxy) maxy = y;
  }
  if (maxx < -margin || minx > cw + margin || maxy < -margin ||
      miny > ch + margin)
    return 0;
  return 1;
}

/* ------------------------------------------------------------------ */
/* main drawing                                                        */

static void draw_block_rec(HDC hdc, DwgModel *m, int bi, const XForm *parent,
                           const DrawOpts *o, int depth, int cw, int ch,
                           int draw_text, PenEnt *pc, TextFontEnt *fc);

static void draw_prim(HDC hdc, KPrim *p, const XForm *xf, const DrawOpts *o,
                      int cw, int ch, int draw_text, PenEnt *pc,
                      TextFontEnt *fc)
{
  if (p->kind == KP_TEXT) {
    draw_text_prim(hdc, p, xf, pc, fc, draw_text, o->bg_white);
    return;
  }
  if (!prim_visible(p, xf, cw, ch))
    return;
  COLORREF col = adjust_color(p->color, o->bg_white);
  if (p->kind == KP_POINT) {
    double x, y;
    xf_apply(xf, p->p[0].x, p->p[0].y, &x, &y);
    /* small cross marker */
    int s = 3;
    HPEN pen = get_pen(pc, col, DPS_SOLID, o->pen_width);
    HPEN old = (HPEN)SelectObject(hdc, pen);
    MoveToEx(hdc, (int)(x - s), (int)y, NULL);
    LineTo(hdc, (int)(x + s), (int)y);
    MoveToEx(hdc, (int)x, (int)(y - s), NULL);
    LineTo(hdc, (int)x, (int)(y + s));
    SelectObject(hdc, old);
    return;
  }
  POINT dev[2048];
  int n = prim_to_dev(p, xf, dev, 2048);
  if (n < 2)
    return;
  HPEN pen = get_pen(pc, col, p->pen, o->pen_width);
  HPEN oldpen = (HPEN)SelectObject(hdc, pen);
  if (p->fill) {
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
    HBRUSH oldbr = (HBRUSH)SelectObject(hdc, br);
    /* WINDING вместо ALTERNATE: убирает ложные «дыры» внутри контура при
       самопересечении петель (частые артефакты штриховок) */
    int oldmode = SetPolyFillMode(hdc, WINDING);
    Polygon(hdc, dev, n);
    SetPolyFillMode(hdc, oldmode);
    SelectObject(hdc, oldbr);
    if (del)
      DeleteObject(br);
  } else {
    Polyline(hdc, dev, n);
  }
  SelectObject(hdc, oldpen);
}

static void draw_block_rec(HDC hdc, DwgModel *m, int bi, const XForm *parent,
                           const DrawOpts *o, int depth, int cw, int ch,
                           int draw_text, PenEnt *pc, TextFontEnt *fc)
{
  if (bi < 0 || bi >= m->n_blocks || depth > 48)
    return;
  MBlock *b = &m->blocks[bi];
  for (int i = 0; i < b->n_prims; i++)
    draw_prim(hdc, &b->prims[i], parent, o, cw, ch, draw_text, pc, fc);
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
                   pc, fc);
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
  draw_block_rec(hdc, m, m->render_block, view, o, 0, cw, ch, draw_text,
                 pens, fonts);
  draw_extent_frame(hdc, m, view, cw, ch, o->bg_white);
  RestoreDC(hdc, saved);

  pen_cache_clear(pens, PEN_CACHE_MAX);
  text_font_clear(fonts);
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