/* model.c - DWG parsing and primitive extraction via LibreDWG
   Part of DWG Viewer plugin for Total Commander (WLX).
   GPLv3+ */
#define _GNU_SOURCE
#include <stdlib.h>
#include <string.h>
#include <stdio.h>
#include <math.h>
#include <wchar.h>

#include "model.h"
#include "aci.h"
#include "draw.h"
#include "dwgload.h"

/* ------------------------------------------------------------------ */
/* tiny dynamic array helpers                                         */

static KPrim *prim_append(MBlock *b)
{
  if (b->n_prims >= b->cap_prims) {
    int nc = b->cap_prims ? b->cap_prims * 2 : 256;
    KPrim *np = (KPrim *)realloc(b->prims, (size_t)nc * sizeof(KPrim));
    if (!np)
      return NULL;
    b->prims = np;
    b->cap_prims = nc;
  }
  KPrim *p = &b->prims[b->n_prims];
  memset(p, 0, sizeof(*p));
  b->n_prims++;
  return p;
}

static MInsert *insert_append(MBlock *b)
{
  if (b->n_inserts >= b->cap_inserts) {
    int nc = b->cap_inserts ? b->cap_inserts * 2 : 64;
    MInsert *ni = (MInsert *)realloc(b->inserts, (size_t)nc * sizeof(MInsert));
    if (!ni)
      return NULL;
    b->inserts = ni;
    b->cap_inserts = nc;
  }
  MInsert *r = &b->inserts[b->n_inserts];
  memset(r, 0, sizeof(*r));
  r->target = -1;
  r->scale[0] = r->scale[1] = 1.0;
  b->n_inserts++;
  return r;
}

/* ------------------------------------------------------------------ */
/* colors                                                              */

static int is_fin(double v)
{
  return !isnan(v) && !isinf(v);
}

static COLORREF color_from_rgb(DWORD rgb)
{
  /* rgb is 0x00RRGGBB */
  BYTE r = (BYTE)((rgb >> 16) & 0xFF);
  BYTE g = (BYTE)((rgb >> 8) & 0xFF);
  BYTE b = (BYTE)(rgb & 0xFF);
  return RGB(r, g, b);
}

/* COLORREF из Dwg_Color с запасным значением (для ByBlock/ByLayer). */
static COLORREF cmc_color(Dwg_Color *c, COLORREF fallback)
{
  if (!c)
    return fallback;
  if (c->method == DWG_COLOR_METHOD_TRUECOLOR && c->rgb)
    return color_from_rgb(c->rgb);
  if (c->index >= 1 && c->index <= 255)
    return aci_color(c->index);
  return fallback;
}

static COLORREF layer_color(Dwg_Data *dwg, Dwg_Object_Ref *lref)
{
  if (!lref)
    return RGB(255, 255, 255);
  Dwg_Object *lobj = dwg_ref_object(dwg, lref);
  if (!lobj || lobj->fixedtype != DWG_TYPE_LAYER)
    return RGB(255, 255, 255);
  Dwg_Object_LAYER *lyr = lobj->tio.object->tio.LAYER;
  if (!lyr)
    return RGB(255, 255, 255);
  Dwg_Color *c = &lyr->color;
  if (c->method == DWG_COLOR_METHOD_TRUECOLOR && c->rgb)
    return color_from_rgb(c->rgb);
  if (c->index >= 1 && c->index <= 255)
    return aci_color(c->index);
  if (c->index == 0) /* bylayer layer color = white-ish */
    return RGB(255, 255, 255);
  return RGB(255, 255, 255);
}

/* returns 1 if the entity's layer is off or frozen */
static int layer_is_off(Dwg_Data *dwg, Dwg_Object_Ref *lref)
{
  if (!lref)
    return 0;
  Dwg_Object *lobj = dwg_ref_object(dwg, lref);
  if (!lobj || lobj->fixedtype != DWG_TYPE_LAYER)
    return 0;
  Dwg_Object_LAYER *lyr = lobj->tio.object->tio.LAYER;
  if (!lyr)
    return 0;
  if (lyr->off || lyr->frozen)
    return 1;
  return 0;
}

static COLORREF ent_color(Dwg_Data *dwg, Dwg_Object_Entity *ent)
{
  Dwg_Color *c = &ent->color;
  if (c->method == DWG_COLOR_METHOD_TRUECOLOR && c->rgb)
    return color_from_rgb(c->rgb);
  if (c->index == 256 || c->method == DWG_COLOR_METHOD_BYLAYER)
    return layer_color(dwg, ent->layer);
  if (c->index == 0 || c->method == DWG_COLOR_METHOD_BYBLOCK) {
    /* ByBlock: typically white */
    return RGB(255, 255, 255);
  }
  if (c->index >= 1 && c->index <= 255)
    return aci_color(c->index);
  if (c->index < 0) /* negative = layer off */
    return layer_color(dwg, ent->layer);
  return RGB(255, 255, 255);
}

/* map entity linetype name to a GDI style */
static int ent_pen_style(Dwg_Data *dwg, Dwg_Object_Entity *ent)
{
  Dwg_Object_Ref *lref = ent->ltype;
  if (!lref)
    return DPS_SOLID;
  Dwg_Object *lobj = dwg_ref_object(dwg, lref);
  if (!lobj || lobj->fixedtype != DWG_TYPE_LTYPE)
    return DPS_SOLID;
  Dwg_Object_LTYPE *lt = lobj->tio.object->tio.LTYPE;
  if (!lt || !lt->name)
    return DPS_SOLID;
  const char *name = lt->name;
  /* normalize: strip leading and compare lowercase */
  static const struct { const char *n; int st; } map[] = {
      {"hidden", DPS_DASH},      {"dashed", DPS_DASH},
      {"center", DPS_DASHDOT},   {"center2", DPS_DASHDOT},
      {"dashdot", DPS_DASHDOT},  {"phantom", DPS_DASHDOTDOT},
      {"bylayer", DPS_SOLID},    {"byblock", DPS_SOLID},
      {"dashed2", DPS_DASH},     {"dashdot2", DPS_DASHDOT},
      {"dot", DPS_DOT},          {"dot2", DPS_DOT},
      {"border", DPS_DASHDOT},   {"divide", DPS_DASHDOTDOT},
      {NULL, DPS_SOLID}};
  /* compare ignoring case and leading/trailing spaces */
  const char *p = name;
  while (*p == ' ' || *p == '\t')
    p++;
  size_t len = strlen(p);
  while (len && (p[len - 1] == ' ' || p[len - 1] == '\t'))
    len--;
  for (int i = 0; map[i].n; i++) {
    if (strlen(map[i].n) == len && strnicmp(p, map[i].n, len) == 0)
      return map[i].st;
  }
  /* GDI style only for well known names; anything else: solid */
  return DPS_SOLID;
}

/* ------------------------------------------------------------------ */
/* text helpers                                                        */

/* strip MTEXT formatting codes and convert to wide.
   The result is owned by the caller (wchar_t*). */
/* Определение UTF-16LE и длины до терминатора.
   LibreDWG для части DWG возвращает text_value как сырые байты UTF-16LE в
   char*.  strlen() тут не годится (у ASCII-символов старший байт = 0x00 и
   обрывает C-строку), поэтому идём по 16-битным единицам:
     - если встретили 0000 на чётной позиции — это терминатор (конец);
     - если старший байт единицы не похож на старший байт BMP-символа
       (0x00 Latin, 0x04/0x05 кириллица и т.п.) — это не UTF-16LE.
   Возвращает длину в байтах (>=0) либо -1, если строка не UTF-16LE. */
static int detect_utf16le_len(const unsigned char *s)
{
  int i = 0, pairs = 0;
  while (i + 1 < 8190) {
    unsigned char lo = s[i];
    unsigned char hi = s[i + 1];
    if (lo == 0 && hi == 0)
      return (i >= 2) ? i : -1; /* нашли терминатор 0000 */
    /* допустимые старшие байты: Latin (00), кириллица (04/05),
       общая пунктуация/символы (20..27) */
    if (!(hi == 0x00 || hi == 0x04 || hi == 0x05 ||
          (hi >= 0x20 && hi <= 0x27)))
      return -1;
    i += 2;
    pairs++;
    /* Защита от чтения «мимо»: раньше здесь использовался strlen(), но для
       UTF-16LE он обрывается на первом нулевом байте и любая строка длиннее
       ~32 символов не распознавалась (текст терялся). Ограничиваем только
       разумной длиной. */
    if (i > 4096 || pairs > 2048)
      return -1;
  }
  return -1;
}

/* Обработка уже «широкой» строки: убираем MTEXT-формат, \P -> перевод строки
   и коды %%c/%%d/%%p. */
static wchar_t *finish_text_w(const wchar_t *in)
{
  wchar_t tmp[4096];
  int n = 0, i = 0;
  while (in[i] && n < 4090) {
    wchar_t c = in[i];
    /* фигурные скобки MTEXT задают формат группы — сами скобки убираем,
       а текст внутри СОХРАНЯЕМ */
    if (c == L'{' || c == L'}') {
      i++;
      continue;
    }
    if (c == L'\\') {
      wchar_t nx = in[i + 1];
      if (nx == L'P') { tmp[n++] = L'\n'; i += 2; continue; }
      if (nx == L'~') { tmp[n++] = L' '; i += 2; continue; }
      if (nx == L'\\') { tmp[n++] = L'\\'; i += 2; continue; }
      if (nx == L'{') { tmp[n++] = L'{'; i += 2; continue; }
      if (nx == L'}') { tmp[n++] = L'}'; i += 2; continue; }
      /* коды с параметрами (до ';'): \A1; \C1; \fArial|...; \H..; \W..; \S..; \Q..; \T..; \p.. */
      if (nx == L'A' || nx == L'a' || nx == L'C' || nx == L'c' ||
          nx == L'F' || nx == L'f' || nx == L'H' || nx == L'h' ||
          nx == L'Q' || nx == L'q' || nx == L'S' || nx == L's' ||
          nx == L'T' || nx == L't' || nx == L'W' || nx == L'w' ||
          nx == L'p') {
        i += 2;
        while (in[i] && in[i] != L';')
          i++;
        if (in[i] == L';')
          i++;
        continue;
      }
      /* прочие переключатели (\L \O \K \N \X...) — без параметров */
      i += 2;
      continue;
    }
    if (c == L'%' && in[i + 1] == L'%' && in[i + 2]) {
      if (in[i + 2] == L'c') { tmp[n++] = 0x2300; i += 3; continue; }
      if (in[i + 2] == L'd') { tmp[n++] = 0x00B0; i += 3; continue; }
      if (in[i + 2] == L'p') { tmp[n++] = 0x00B1; i += 3; continue; }
      if (in[i + 2] == L'%') { tmp[n++] = L'%'; i += 3; continue; }
    }
    tmp[n++] = c;
    i++;
  }
  tmp[n] = 0;
  if (n == 0)
    return NULL;
  wchar_t *out = (wchar_t *)malloc(((size_t)n + 1) * sizeof(wchar_t));
  if (out)
    wcscpy(out, tmp);
  return out;
}

/* Проверка, что строка является корректной UTF-8.
   Многие русские DWG (особенно R2000-R2004 и файлы из русских версий CAD)
   хранят текст в кодировке Windows-1251.  В этом случае строка НЕ является
   валидным UTF-8, и мы декодируем её как CP1251. */
static int is_valid_utf8(const unsigned char *s)
{
  if (!s)
    return 0;
  size_t len = strlen((const char *)s);
  size_t i = 0;
  while (i < len) {
    unsigned char c = s[i];
    if (c < 0x80) {
      i++;
    } else if ((c & 0xE0) == 0xC0) {
      if (i + 1 >= len || (s[i + 1] & 0xC0) != 0x80)
        return 0;
      i += 2;
    } else if ((c & 0xF0) == 0xE0) {
      if (i + 2 >= len || (s[i + 1] & 0xC0) != 0x80 ||
          (s[i + 2] & 0xC0) != 0x80)
        return 0;
      i += 3;
    } else if ((c & 0xF8) == 0xF0) {
      if (i + 3 >= len || (s[i + 1] & 0xC0) != 0x80 ||
          (s[i + 2] & 0xC0) != 0x80 || (s[i + 3] & 0xC0) != 0x80)
        return 0;
      i += 4;
    } else {
      return 0; /* недопустимый ведущий байт (0x80-0xBF, 0xF8-0xFF) */
    }
  }
  return 1;
}

static wchar_t *make_text_w(const char *utf8)
{
  if (!utf8)
    return NULL;
  /* LibreDWG возвращает часть текстов как сырые байты UTF-16LE в char*. */
  {
    int rawlen = detect_utf16le_len((const unsigned char *)utf8);
    if (rawlen >= 2) {
      wchar_t wbuf[4096];
      int m = 0;
      for (int i = 0; i + 1 < rawlen && m < 4090; i += 2) {
        unsigned w = (unsigned)(unsigned char)utf8[i] |
                     ((unsigned)(unsigned char)utf8[i + 1] << 8);
        if (w == 0)
          break;
        wbuf[m++] = (wchar_t)w;
      }
      wbuf[m] = 0;
      return finish_text_w(wbuf);
    }
  }
  int utf8_ok = is_valid_utf8((const unsigned char *)utf8);
  /* декодируем (UTF-8 либо CP1251) в широкую строку, а MTEXT-коды
     обрабатываем единообразно в finish_text_w() */
  wchar_t wbuf[4096];
  int n = 0;
  const unsigned char *s = (const unsigned char *)utf8;
  while (*s && n < 4090) {
    if (*s < 0x80) {
      wbuf[n++] = (wchar_t)*s;
      s++;
    } else if (utf8_ok && (*s & 0xE0) == 0xC0 && s[1] &&
               (s[1] & 0xC0) == 0x80) {
      wbuf[n++] = (wchar_t)(((*s & 0x1F) << 6) | (s[1] & 0x3F));
      s += 2;
    } else if (utf8_ok && (*s & 0xF0) == 0xE0 && s[1] && s[2] &&
               (s[1] & 0xC0) == 0x80 && (s[2] & 0xC0) == 0x80) {
      wbuf[n++] = (wchar_t)(((*s & 0x0F) << 12) | ((s[1] & 0x3F) << 6) |
                            (s[2] & 0x3F));
      s += 3;
    } else if (utf8_ok && (*s & 0xF8) == 0xF0 && s[1] && s[2] && s[3] &&
               (s[1] & 0xC0) == 0x80 && (s[2] & 0xC0) == 0x80 &&
               (s[3] & 0xC0) == 0x80) {
      wchar_t c = (wchar_t)((((*s & 0x07) << 18) | ((s[1] & 0x3F) << 12) |
                            ((s[2] & 0x3F) << 6) | (s[3] & 0x3F)) - 0x10000);
      if (c < 0xD800 || c >= 0xDC00)
        wbuf[n++] = c + 0xDC00;
      s += 4;
    } else {
      unsigned char b = *s;
      if (b == 0xA8)
        wbuf[n++] = 0x0401;
      else if (b == 0xB8)
        wbuf[n++] = 0x0451;
      else if (b >= 0xC0 && b <= 0xDF)
        wbuf[n++] = (wchar_t)(0x0410 + (b - 0xC0));
      else if (b >= 0xE0 && b <= 0xFF)
        wbuf[n++] = (wchar_t)(0x0430 + (b - 0xE0));
      else
        wbuf[n++] = (wchar_t)b;
      s++;
    }
  }
  wbuf[n] = 0;
  return finish_text_w(wbuf);
}

static void emit_text_wrap(MBlock *b, MPt pos, double height, double rot,
                           const char *utf8, COLORREF color, double wrap);

static void emit_text(MBlock *b, MPt pos, double height, double rot,
                      const char *utf8, COLORREF color)
{
  emit_text_wrap(b, pos, height, rot, utf8, color, 0.0);
}

static void emit_text_wrap(MBlock *b, MPt pos, double height, double rot,
                           const char *utf8, COLORREF color, double wrap)
{
  if (height <= 0 || height > 1e12)
    height = 1e-5;
  if (!is_fin(height) || !is_fin(pos.x) || !is_fin(pos.y))
    return;
  wchar_t *w = make_text_w(utf8);
  if (!w)
    return;
  KPrim *p = prim_append(b);
  if (!p) { free(w); return; }
  p->kind = KP_TEXT;
  p->color = color;
  p->pos = pos;
  p->height = height;
  p->rot = rot;
  p->wrap_width = (is_fin(wrap) && wrap > 0) ? wrap : 0.0;
  p->text = w;
}

/* ------------------------------------------------------------------ */
/* geometry emitters                                                   */

static void emit_line(MBlock *b, double x1, double y1, double x2, double y2,
                      COLORREF color, int pen)
{
  if (!is_fin(x1) || !is_fin(y1) || !is_fin(x2) || !is_fin(y2))
    return;
  KPrim *p = prim_append(b);
  if (!p)
    return;
  p->kind = KP_POLY;
  p->color = color;
  p->pen = pen;
  p->n = 0;
  p->p = (MPt *)malloc(2 * sizeof(MPt));
  if (!p->p) { b->n_prims--; return; }
  p->p[0].x = x1; p->p[0].y = y1;
  p->p[1].x = x2; p->p[1].y = y2;
  p->n = 2;
}

static void emit_polygon(MBlock *b, const MPt *pts, int n,
                         COLORREF color, int pen, int fill)
{
  if (n < 2)
    return;
  for (int i = 0; i < n; i++) {
    if (!is_fin(pts[i].x) || !is_fin(pts[i].y))
      return;
  }
  KPrim *p = prim_append(b);
  if (!p)
    return;
  p->kind = KP_POLY;
  p->color = color;
  p->pen = pen;
  p->fill = fill;
  p->n = n;
  p->p = (MPt *)malloc((size_t)n * sizeof(MPt));
  if (!p->p) { b->n_prims--; return; }
  memcpy(p->p, pts, (size_t)n * sizeof(MPt));
}

static void emit_arc_pts(MBlock *b, double cx, double cy, double r,
                         double a0, double a1, COLORREF color, int pen)
{
  if (!is_fin(cx) || !is_fin(cy) || !is_fin(r) || r <= 0)
    return;
  if (!is_fin(a0) || !is_fin(a1))
    return;
  double span = a1 - a0;
  while (span < 0)
    span += 2 * M_PI;
  while (span > 2 * M_PI)
    span -= 2 * M_PI;
  int n = (int)(span / (M_PI / 30.0)) + 2;
  if (n < 4)
    n = 4;
  if (n > 480)
    n = 480;
  MPt *pts = (MPt *)malloc((size_t)n * sizeof(MPt));
  if (!pts)
    return;
  for (int i = 0; i < n; i++) {
    double a = a0 + span * (double)i / (double)(n - 1);
    pts[i].x = cx + r * cos(a);
    pts[i].y = cy + r * sin(a);
  }
  emit_polygon(b, pts, n, color, pen, 0);
  free(pts);
}

/* Дуга со знаковым размахом span (может быть отрицательным: по часовой).
   Раньше здесь использовалась emit_arc_pts(), которая всегда «доводит»
   размах до положительного — из-за этого CW-дуги булджей превращались в
   почти полную окружность (гигантские эллипсы в масштабированных блоках). */
static void emit_arc_signed(MBlock *b, double cx, double cy, double r,
                            double a0, double span, COLORREF color, int pen)
{
  if (!is_fin(cx) || !is_fin(cy) || !is_fin(r) || r <= 0)
    return;
  if (!is_fin(a0) || !is_fin(span))
    return;
  double as = fabs(span);
  if (as < 1e-12)
    return;
  int n = (int)(as / (M_PI / 30.0)) + 2;
  if (n < 4)
    n = 4;
  if (n > 480)
    n = 480;
  MPt *pts = (MPt *)malloc((size_t)n * sizeof(MPt));
  if (!pts)
    return;
  for (int i = 0; i < n; i++) {
    double a = a0 + span * (double)i / (double)(n - 1);
    pts[i].x = cx + r * cos(a);
    pts[i].y = cy + r * sin(a);
  }
  emit_polygon(b, pts, n, color, pen, 0);
  free(pts);
}

/* Булдж -> параметры дуги (центр, радиус, начало, знаковый размах).
   b = tan(theta/4), theta — знаковый вписанный угол (b>0 — против часовой).
   Возвращает 1 при успехе. */
static int bulge_to_arc(MPt p0, MPt p1, double b,
                        double *cx, double *cy, double *r,
                        double *a0, double *span)
{
  if (!is_fin(b) || b == 0.0)
    return 0;
  double dx = p1.x - p0.x, dy = p1.y - p0.y;
  double c = sqrt(dx * dx + dy * dy);
  if (!(c > 1e-12))
    return 0;
  double theta = 4.0 * atan(b);
  if (!is_fin(theta) || fabs(theta) < 1e-12)
    return 0;
  double rr = c / (2.0 * sin(theta / 2.0));
  if (!is_fin(rr))
    return 0;
  double R = fabs(rr);
  double sag = fabs(b) * c / 2.0;   /* стрелка прогиба */
  double h = R - sag;
  double perpx = -dy / c, perpy = dx / c; /* левая нормаль к хорде */
  double sign = (b > 0.0) ? 1.0 : -1.0;   /* центр — напротив прогиба */
  double mx = (p0.x + p1.x) / 2.0, my = (p0.y + p1.y) / 2.0;
  *cx = mx + sign * perpx * h;
  *cy = my + sign * perpy * h;
  *r = R;
  *a0 = atan2(p0.y - *cy, p0.x - *cx);
  *span = theta;
  return 1;
}

static void emit_point(MBlock *b, double x, double y, COLORREF color)
{
  if (!is_fin(x) || !is_fin(y))
    return;
  KPrim *p = prim_append(b);
  if (!p)
    return;
  p->kind = KP_POINT;
  p->color = color;
  p->n = 1;
  p->p = (MPt *)malloc(sizeof(MPt));
  if (!p->p) { b->n_prims--; return; }
  p->p[0].x = x;
  p->p[0].y = y;
}

static void emit_spline(MBlock *b, Dwg_Entity_SPLINE *spl, COLORREF color,
                        int pen)
{
  if (!spl || !spl->ctrl_pts || spl->num_ctrl_pts < 2)
    return;
  int n = (int)spl->num_ctrl_pts;
  int deg = spl->degree;
  if (deg < 1 || deg > 6)
    deg = 3;
  unsigned long long m = (unsigned long long)n + (unsigned long long)deg + 1ULL;
  double *knots = spl->knots;
  unsigned long long num_knots = spl->num_knots;
  if (!knots || num_knots != m) {
    /* build uniform clamped knots */
    knots = (double *)malloc((size_t)m * sizeof(double));
    if (!knots)
      return;
    for (unsigned long long i = 0; i < m; i++) {
      if (i <= (unsigned long long)deg)
        knots[i] = 0.0;
      else if (i >= (unsigned long long)n)
        knots[i] = 1.0;
      else
        knots[i] = (double)(i - deg) / (double)(n - deg);
    }
    num_knots = m;
  }
  double u0 = knots[deg];
  double u1 = knots[(size_t)n]; /* m-1-deg == n */
  if (u1 <= u0)
    u1 = u0 + 1.0;
  int steps = n * 8;
  if (steps < 24)
    steps = 24;
  if (steps > 2000)
    steps = 2000;
  MPt *pts = (MPt *)malloc(((size_t)steps + 1) * sizeof(MPt));
  if (!pts) {
    if (num_knots != spl->num_knots)
      free(knots);
    return;
  }
  int np = 0;
  double d[32][2]; /* degree raised max 6 */
  int dd[32];
  for (int i0 = 0; i0 <= steps; i0++) {
    double u = u0 + (u1 - u0) * (double)i0 / (double)steps;
    /* find knot span k: U[k] <= u < U[k+1] */
    int k = deg;
    for (int j = deg; j < (int)(num_knots - 1); j++) {
      if (u >= knots[j] && u < knots[j + 1]) {
        k = j;
        break;
      }
    }
    if (u >= knots[(size_t)(num_knots - 1)]) {
      /* at the very end of the clamped knot vector: use the last valid
         span, otherwise de Boor knot indexing runs past the array */
      k = n - 1;
    }
    /* valid spans are k in [deg, n-1] */
    if (k > n - 1)
      k = n - 1;
    if (k < deg)
      k = deg;
    /* de Boor with a working vector of exactly deg+1 entries,
       mapped to control points k-deg .. k */
    int w = deg;                     /* last slot of the working vector */
    for (int j = 0; j <= w; j++) {
      int abs = k - deg + j;
      if (abs >= 0 && abs < n) {
        d[j][0] = spl->ctrl_pts[abs].x;
        d[j][1] = spl->ctrl_pts[abs].y;
        dd[j] = 1;
      } else {
        dd[j] = 0;
      }
    }
    for (int r = 1; r <= deg; r++) {
      for (int i = k - deg + r; i <= k; i++) {
        int idx = i - (k - deg);     /* in [r, deg] */
        if (idx - 1 < 0 || idx > w)
          continue;
        double denom = knots[(size_t)(i + deg - r + 1)] - knots[(size_t)i];
        double alpha = (denom != 0.0)
            ? ((u - knots[(size_t)i]) / denom)
            : 0.0;
        d[idx][0] = (1.0 - alpha) * d[idx - 1][0] + alpha * d[idx][0];
        d[idx][1] = (1.0 - alpha) * d[idx - 1][1] + alpha * d[idx][1];
        dd[idx] = 1;
      }
    }
    if (dd[w]) {
      pts[np].x = d[w][0];
      pts[np].y = d[w][1];
      np++;
    }
  }
  if (num_knots != spl->num_knots)
    free(knots);
  if (np >= 2) {
    int closed = spl->closed_b || spl->periodic ||
                 (spl->splineflags & SPLINE_SPLINEFLAGS_CLOSED);
    emit_polygon(b, pts, np, color, pen, 0);
    if (closed) {
      KPrim *last = &b->prims[b->n_prims - 1];
      /* already handled by close flag? polygon is open; append closing point */
      MPt p0 = pts[0];
      if (last->n > 0 && p0.x == last->p[last->n - 1].x && 0 == 0) {
        /* skip duplicate close */
      }
      (void)p0;
    }
  }
  free(pts);
}

/* polyline with bulges: points array + optional per-vertex bulges.
   If bulge != 0 between p[i] and p[i+1], an arc is emitted. */
static void emit_polyline_bulge(MBlock *b, const MPt *pts, int n,
                                const double *bulges, int closed,
                                COLORREF color, int pen)
{
  if (n < 2)
    return;
  for (int i = 0; i < n; i++) {
    if (!is_fin(pts[i].x) || !is_fin(pts[i].y))
      return;
  }
  int segs = closed ? n : n - 1;
  for (int i = 0; i < segs; i++) {
    int i0 = i;
    int i1 = closed ? (i + 1) % n : i + 1;
    double bx = bulges ? bulges[i0] : 0.0;
    double cx, cy, r, a0, span;
    if (bx != 0.0 &&
        bulge_to_arc(pts[i0], pts[i1], bx, &cx, &cy, &r, &a0, &span)) {
      emit_arc_signed(b, cx, cy, r, a0, span, color, pen);
    } else {
      emit_line(b, pts[i0].x, pts[i0].y, pts[i1].x, pts[i1].y, color, pen);
    }
  }
}

/* ------------------------------------------------------------------ */
/* entity emitters                                                     */

static void entity_line(MBlock *b, Dwg_Data *dwg, Dwg_Entity_LINE *e,
                        COLORREF color, int pen)
{
  (void)dwg;
  if (!e)
    return;
  emit_line(b, e->start.x, e->start.y, e->end.x, e->end.y, color, pen);
}

static void entity_circle(MBlock *b, Dwg_Entity_CIRCLE *e, COLORREF color,
                          int pen)
{
  if (!e)
    return;
  emit_arc_pts(b, e->center.x, e->center.y, e->radius, 0, 2 * M_PI, color,
               pen);
}

static void entity_arc(MBlock *b, Dwg_Entity_ARC *e, COLORREF color, int pen)
{
  if (!e)
    return;
  emit_arc_pts(b, e->center.x, e->center.y, e->radius, e->start_angle,
               e->end_angle, color, pen);
}

static void entity_ellipse(MBlock *b, Dwg_Entity_ELLIPSE *e, COLORREF color,
                           int pen)
{
  if (!e)
    return;
  double majx = e->sm_axis.x;
  double majy = e->sm_axis.y;
  double ratio = e->axis_ratio;
  if (ratio <= 0 || ratio > 1)
    ratio = 1.0;
  double a0 = e->start_angle;
  double a1 = e->end_angle;
  if (!is_fin(a0) || !is_fin(a1)) {
    a0 = 0;
    a1 = 2 * M_PI;
  }
  double span = a1 - a0;
  while (span < 0)
    span += 2 * M_PI;
  while (span > 2 * M_PI)
    span -= 2 * M_PI;
  if (span < 1e-9)
    span = 2 * M_PI;
  int n = (int)(span / (M_PI / 24.0)) + 2;
  if (n < 8)
    n = 8;
  if (n > 400)
    n = 400;
  /* minor axis = perpendicular of major * ratio */
  double mx = -majy * ratio;
  double my = majx * ratio;
  MPt *pts = (MPt *)malloc((size_t)n * sizeof(MPt));
  if (!pts)
    return;
  for (int i = 0; i < n; i++) {
    double t = a0 + span * (double)i / (double)(n - 1);
    double c = cos(t), s = sin(t);
    pts[i].x = e->center.x + majx * c + mx * s;
    pts[i].y = e->center.y + majy * c + my * s;
  }
  emit_polygon(b, pts, n, color, pen, 0);
  free(pts);
}

static void entity_lwpolyline(MBlock *b, Dwg_Data *dwg, Dwg_Entity_LWPOLYLINE *e,
                              COLORREF color, int pen)
{
  (void)dwg;
  if (!e || !e->points || e->num_points < 2)
    return;
  unsigned long np = e->num_points;
  MPt *pts = (MPt *)malloc((np > 0 ? (size_t)np : 1) * sizeof(MPt));
  if (!pts)
    return;
  for (unsigned long i = 0; i < np; i++) {
    pts[i].x = e->points[i].x;
    pts[i].y = e->points[i].y;
  }
  int closed = (e->flag & 1) || (e->flag & 0x200);
  /* copy bulges into a zero-padded local array sized num_points */
  double *bulges = NULL;
  if (e->bulges && e->num_bulges > 0) {
    bulges = (double *)calloc(np > 0 ? (size_t)np : 1, sizeof(double));
    if (!bulges) {
      free(pts);
      return;
    }
    unsigned long ncopy = e->num_bulges < np ? e->num_bulges : np;
    for (unsigned long i = 0; i < ncopy; i++)
      bulges[i] = e->bulges[i];
  }
  emit_polyline_bulge(b, pts, (int)np, bulges, closed, color, pen);
  free(bulges);
  free(pts);
}

static void entity_polyline(MBlock *b, Dwg_Data *dwg, Dwg_Object_Ref **vertex,
                            unsigned long num_owned, int closed,
                            COLORREF color, int pen)
{
  if (!vertex || num_owned == 0)
    return;
  int cap = (int)num_owned;
  if (cap > 100000)
    cap = 100000;
  MPt *pts = (MPt *)malloc((size_t)cap * sizeof(MPt));
  double *bulges = (double *)malloc((size_t)cap * sizeof(double));
  if (!pts || !bulges) {
    free(pts);
    free(bulges);
    return;
  }
  int n = 0;
  for (int i = 0; i < cap; i++) {
    Dwg_Object_Ref *vref = vertex[i];
    if (!vref)
      continue;
    Dwg_Object *vobj = dwg_ref_object(dwg, vref);
    if (!vobj || vobj->supertype != DWG_SUPERTYPE_ENTITY ||
        !vobj->tio.entity)
      continue;
    if (vobj->fixedtype == DWG_TYPE_VERTEX_2D) {
      Dwg_Entity_VERTEX_2D *v = vobj->tio.entity->tio.VERTEX_2D;
      if (!v)
        continue;
      pts[n].x = v->point.x;
      pts[n].y = v->point.y;
      bulges[n] = v->bulge;
    } else if (vobj->fixedtype == DWG_TYPE_VERTEX_3D) {
      Dwg_Entity_VERTEX_3D *v = vobj->tio.entity->tio.VERTEX_3D;
      if (!v)
        continue;
      pts[n].x = v->point.x;
      pts[n].y = v->point.y;
      bulges[n] = 0.0;
    } else {
      continue;
    }
    n++;
  }
  if (n >= 2)
    emit_polyline_bulge(b, pts, n, bulges, closed, color, pen);
  free(pts);
  free(bulges);
}

static void entity_text(MBlock *b, Dwg_Data *dwg, Dwg_Entity_TEXT *e,
                        COLORREF color)
{
  (void)dwg;
  if (!e)
    return;
  MPt pos = {e->ins_pt.x, e->ins_pt.y};
  emit_text(b, pos, e->height, e->rotation, e->text_value, color);
}

static void entity_mtext(MBlock *b, Dwg_Data *dwg, Dwg_Entity_MTEXT *e,
                         COLORREF color)
{
  (void)dwg;
  if (!e)
    return;
  double rot = 0;
  if (is_fin(e->x_axis_dir.x) && is_fin(e->x_axis_dir.y))
    rot = atan2(e->x_axis_dir.y, e->x_axis_dir.x);
  MPt pos = {e->ins_pt.x, e->ins_pt.y};
  emit_text_wrap(b, pos, e->text_height, rot, e->text, color, e->rect_width);
}

static void entity_leader(MBlock *b, Dwg_Entity_LEADER *e, COLORREF color,
                          int pen)
{
  if (!e || !e->points || e->num_points < 2)
    return;
  unsigned long np = e->num_points;
  if (np < 2 || np > 100000)
    return;
  MPt *pts = (MPt *)malloc((size_t)np * sizeof(MPt));
  if (!pts)
    return;
  for (unsigned long i = 0; i < np; i++) {
    pts[i].x = e->points[i].x;
    pts[i].y = e->points[i].y;
  }
  emit_polyline_bulge(b, pts, (int)np, NULL, 0, color, pen);
  free(pts);
}

static void handle_insert(MBlock *b, const Dwg_Entity_INSERT *ins,
                          Dwg_Data *dwg, DwgModel *m);

static void entity_dimension(MBlock *b, Dwg_Data *dwg, DwgModel *m,
                             Dwg_DIMENSION_common *d, COLORREF color)
{
  if (!d)
    return;
  /* Размеры хранят всю графику (линии, стрелки, текст) в связанном
     анонимном блоке — разворачиваем его как вставку в начале координат. */
  if (d->block) {
    Dwg_Entity_INSERT tmp;
    memset(&tmp, 0, sizeof(tmp));
    tmp.block_header = d->block;
    tmp.scale.x = tmp.scale.y = tmp.scale.z = 1.0;
    handle_insert(b, &tmp, dwg, m);
    return;
  }
  /* Запасной вариант (нет блока): линия определений + текст. */
  MPt p1 = { d->def_pt.x, d->def_pt.y };
  MPt p2 = { d->text_midpt.x, d->text_midpt.y };
  emit_line(b, p1.x, p1.y, p2.x, p2.y, color, DPS_SOLID);
  if (d->user_text) {
    double len = hypot(p2.x - p1.x, p2.y - p1.y);
    double h = len > 0 ? len / 50.0 : 1e-3;
    emit_text(b, p2, h, d->text_rotation, d->user_text, color);
  }
}

/* Мультивыноска (MULTILEADER): линии выносок + содержимое (текст или блок). */
static void entity_multileader(MBlock *b, Dwg_Data *dwg, DwgModel *m,
                               Dwg_Entity_MULTILEADER *ml, COLORREF color,
                               int pen)
{
  if (!ml)
    return;
  Dwg_MLEADER_AnnotContext *ctx = &ml->ctx;
  /* линии выносок */
  unsigned long nl = ctx->num_leaders;
  if (ctx->leaders && nl) {
    if (nl > 1000)
      nl = 1000;
    for (unsigned long i = 0; i < nl; i++) {
      Dwg_LEADER_Node *nd = &ctx->leaders[i];
      unsigned long nlines = nd->num_lines;
      if (!nd->lines || !nlines)
        continue;
      if (nlines > 64)
        nlines = 64;
      for (unsigned long j = 0; j < nlines; j++) {
        Dwg_LEADER_Line *ln = &nd->lines[j];
        unsigned long np = ln->num_points;
        if (!ln->points || np < 2)
          continue;
        if (np > 1000)
          np = 1000;
        MPt *pts = (MPt *)malloc((size_t)np * sizeof(MPt));
        if (!pts)
          continue;
        for (unsigned long k = 0; k < np; k++) {
          pts[k].x = ln->points[k].x;
          pts[k].y = ln->points[k].y;
        }
        emit_polygon(b, pts, (int)np, color, pen, 0);
        free(pts);
      }
    }
  }
  /* содержимое: текст или блок */
  if (ctx->has_content_txt) {
    Dwg_MLEADER_Content_MText *t = &ctx->content.txt;
    double h = (t->height > 0) ? t->height : ctx->text_height;
    MPt pos = { t->location.x, t->location.y };
    emit_text(b, pos, h, t->rotation, t->default_text,
              cmc_color(&t->color, color));
  } else if (ctx->has_content_blk) {
    Dwg_MLEADER_Content_Block *blk = &ctx->content.blk;
    Dwg_Entity_INSERT tmp;
    memset(&tmp, 0, sizeof(tmp));
    tmp.block_header = blk->block_table;
    tmp.ins_pt.x = blk->location.x;
    tmp.ins_pt.y = blk->location.y;
    tmp.rotation = blk->rotation;
    tmp.scale.x = (blk->scale.x != 0) ? blk->scale.x : 1.0;
    tmp.scale.y = (blk->scale.y != 0) ? blk->scale.y : 1.0;
    tmp.scale.z = (blk->scale.z != 0) ? blk->scale.z : 1.0;
    handle_insert(b, &tmp, dwg, m);
  }
}

/* ------------------------------------------------------------------ */
/* entities: HATCH, 3DSOLID/REGION/BODY, IMAGE, ATTRIB                */

static void hatch_path_polyline(MBlock *b, Dwg_HATCH_Path *path,
                                COLORREF color, int pen)
{
  Dwg_HATCH_PolylinePath *pl = path->polyline_paths;
  unsigned long n = path->num_segs_or_paths;
  if (!pl || n == 0)
    return;
  if (n > 200000)
    n = 200000;
  MPt *pts = (MPt *)malloc((size_t)n * sizeof(MPt));
  double *bgs = (double *)malloc((size_t)n * sizeof(double));
  if (!pts || !bgs) {
    free(pts);
    free(bgs);
    return;
  }
  for (unsigned long i = 0; i < n; i++) {
    pts[i].x = pl[i].point.x;
    pts[i].y = pl[i].point.y;
    bgs[i] = path->bulges_present ? pl[i].bulge : 0.0;
  }
  int closed = (path->flag & 0x20) == 0;
  emit_polyline_bulge(b, pts, (int)n, bgs, closed, color, pen);
  free(bgs);
  free(pts);
}

/* ---- дуги HATCH ----
   Углы дуг HATCH в DWG заданы в OCS-параметризации, которая может быть
   зеркальной по оси X. «Прямое» использование углов давало почти полную
   окружность (гигантские «круги» при заливке). Зеркальность sgn=+1/-1
   выбираем по совпадению начала дуги с концом предыдущего сегмента, а при
   отсутствии опоры — по флагу is_ccw. */

static MPt hatch_arc_pt(double cx, double cy, double r, double sgn, double a)
{
  MPt p;
  p.x = cx + r * cos(sgn * a);
  p.y = cy + r * sin(sgn * a);
  return p;
}

static MPt hatch_ell_pt(double cx, double cy, double ex, double ey, double ratio,
                        double sgn, double t)
{
  MPt p;
  double c = cos(sgn * t), s = sin(sgn * t);
  p.x = cx + ex * c - ey * ratio * s;
  p.y = cy + ey * c + ex * ratio * s;
  return p;
}

/* Выбор зеркальности: по совпадению с опорной точкой ref (если есть),
   иначе по направлению is_ccw. */
static double hatch_pick_sign(MPt p1, MPt p2, MPt ref, int have_ref, int is_ccw)
{
  if (!have_ref)
    return is_ccw ? 1.0 : -1.0;
  double d1 = (p1.x - ref.x) * (p1.x - ref.x) + (p1.y - ref.y) * (p1.y - ref.y);
  double d2 = (p2.x - ref.x) * (p2.x - ref.x) + (p2.y - ref.y) * (p2.y - ref.y);
  return (d2 < d1) ? -1.0 : 1.0;
}

static int hatch_arc_add(double cx, double cy, double r, double a0, double a1,
                         double sgn, int skip_first, MPt *out, int cap)
{
  int n = 0;
  if (!is_fin(cx) || !is_fin(cy) || !is_fin(r) || r <= 0)
    return 0;
  if (!is_fin(a0) || !is_fin(a1))
    return 0;
  double sweep = a1 - a0;
  while (sweep < 0)
    sweep += 2 * M_PI;
  while (sweep >= 2 * M_PI)
    sweep -= 2 * M_PI;
  if (sweep < 1e-12)
    sweep = 2 * M_PI; /* совпадающие углы — полная окружность */
  int steps = (int)(sweep / (M_PI / 30.0)) + 1;
  if (steps < 2)
    steps = 2;
  if (steps > 240)
    steps = 240;
  int i0 = (skip_first && steps > 1) ? 1 : 0;
  for (int i = i0; i <= steps && n < cap; i++) {
    double a = a0 + sweep * (double)i / (double)steps;
    out[n] = hatch_arc_pt(cx, cy, r, sgn, a);
    n++;
  }
  return n;
}

static int hatch_ell_add(double cx, double cy, double ex, double ey, double ratio,
                         double a0, double a1, double sgn, int skip_first,
                         MPt *out, int cap)
{
  int n = 0;
  if (!is_fin(cx) || !is_fin(cy) || !is_fin(ex) || !is_fin(ey))
    return 0;
  if (!(ratio > 0))
    ratio = 1.0;
  double sweep = a1 - a0;
  while (sweep < 0)
    sweep += 2 * M_PI;
  while (sweep >= 2 * M_PI)
    sweep -= 2 * M_PI;
  if (sweep < 1e-12)
    sweep = 2 * M_PI;
  int steps = (int)(sweep / (M_PI / 30.0)) + 1;
  if (steps < 2)
    steps = 2;
  if (steps > 240)
    steps = 240;
  int i0 = (skip_first && steps > 1) ? 1 : 0;
  for (int i = i0; i <= steps && n < cap; i++) {
    double t = a0 + sweep * (double)i / (double)steps;
    out[n] = hatch_ell_pt(cx, cy, ex, ey, ratio, sgn, t);
    n++;
  }
  return n;
}

static void hatch_path_segs(MBlock *b, Dwg_HATCH_Path *path, COLORREF color,
                            int pen)
{
  Dwg_HATCH_PathSeg *segs = path->segs;
  unsigned long n = path->num_segs_or_paths;
  if (!segs || n == 0)
    return;
  if (n > 200000)
    n = 200000;
  MPt last = { 0, 0 };
  int have = 0;
  for (unsigned long i = 0; i < n; i++) {
    Dwg_HATCH_PathSeg *s = &segs[i];
    switch (s->curve_type) {
    case 1: /* line */
      emit_line(b, s->first_endpoint.x, s->first_endpoint.y,
                s->second_endpoint.x, s->second_endpoint.y, color, pen);
      last.x = s->second_endpoint.x;
      last.y = s->second_endpoint.y;
      have = 1;
      break;
    case 2: { /* circular arc */
      MPt p1 = hatch_arc_pt(s->center.x, s->center.y, s->radius, 1.0,
                            s->start_angle);
      MPt p2 = hatch_arc_pt(s->center.x, s->center.y, s->radius, -1.0,
                            s->start_angle);
      double sgn = hatch_pick_sign(p1, p2, last, have, s->is_ccw);
      MPt tmp[260];
      int m = hatch_arc_add(s->center.x, s->center.y, s->radius,
                            s->start_angle, s->end_angle, sgn, 0, tmp, 260);
      if (m >= 2)
        emit_polygon(b, tmp, m, color, pen, 0);
      if (m > 0) { last = tmp[m - 1]; have = 1; }
      break;
    }
    case 3: { /* elliptical arc */
      double cxx = s->center.x, cyy = s->center.y;
      double ex = s->endpoint.x - cxx, ey = s->endpoint.y - cyy;
      MPt p1 = hatch_ell_pt(cxx, cyy, ex, ey, s->minor_major_ratio, 1.0,
                            s->start_angle);
      MPt p2 = hatch_ell_pt(cxx, cyy, ex, ey, s->minor_major_ratio, -1.0,
                            s->start_angle);
      double sgn = hatch_pick_sign(p1, p2, last, have, s->is_ccw);
      MPt tmp[260];
      int m = hatch_ell_add(cxx, cyy, ex, ey, s->minor_major_ratio,
                            s->start_angle, s->end_angle, sgn, 0, tmp, 260);
      if (m >= 2)
        emit_polygon(b, tmp, m, color, pen, 0);
      if (m > 0) { last = tmp[m - 1]; have = 1; }
      break;
    }
    case 4: /* spline via control points */
      if (s->control_points && s->num_control_points >= 2) {
        unsigned long m = s->num_control_points;
        if (m > 2000)
          m = 2000;
        MPt *pts = (MPt *)malloc((size_t)m * sizeof(MPt));
        if (pts) {
          for (unsigned long j = 0; j < m; j++) {
            pts[j].x = s->control_points[j].point.x;
            pts[j].y = s->control_points[j].point.y;
          }
          emit_polygon(b, pts, (int)m, color, pen, 0);
          free(pts);
        }
      }
      break;
    }
  }
}

/* Построить замкнутый контур петли штриховки (дуги/булджи упрощаются).
   Возвращает массив точек (free) и их число; при неудаче n=0. */
static MPt *hatch_loop_pts(Dwg_HATCH_Path *path, int *out_n)
{
  *out_n = 0;
  int cap = 4096;
  MPt *pt = (MPt *)malloc((size_t)cap * sizeof(MPt));
  if (!pt)
    return NULL;
  int n = 0;
  if (path->flag & 2) { /* полилиния с булджами */
    Dwg_HATCH_PolylinePath *pl = path->polyline_paths;
    unsigned long np = path->num_segs_or_paths;
    if (!pl || np < 2) {
      free(pt);
      return NULL;
    }
    for (unsigned long i = 0; i < np && n < cap; i++) {
      pt[n].x = pl[i].point.x;
      pt[n].y = pl[i].point.y;
      n++;
      double bg = path->bulges_present ? pl[i].bulge : 0.0;
      if (bg != 0.0 && n < cap - 2) {
        MPt a = { pl[i].point.x, pl[i].point.y };
        MPt c_ = { pl[(i + 1) % np].point.x, pl[(i + 1) % np].point.y };
        double cx, cy, r, t0, span;
        if (bulge_to_arc(a, c_, bg, &cx, &cy, &r, &t0, &span)) {
          double as = fabs(span);
          int steps = (int)(as / (M_PI / 30.0)) + 1;
          if (steps < 2)
            steps = 2;
          if (steps > 240)
            steps = 240;
          for (int k = 1; k <= steps && n < cap; k++) {
            double t = t0 + span * (double)k / (double)steps;
            pt[n].x = cx + r * cos(t);
            pt[n].y = cy + r * sin(t);
            n++;
          }
        }
      }
    }
  } else { /* сегменты: линии/дуги/эллипсы/сплайны */
    Dwg_HATCH_PathSeg *sg = path->segs;
    unsigned long ns = path->num_segs_or_paths;
    if (!sg || ns == 0) {
      free(pt);
      return NULL;
    }
    MPt last = { 0, 0 };
    int have = 0;
    for (unsigned long i = 0; i < ns && n < cap; i++) {
      Dwg_HATCH_PathSeg *s = &sg[i];
      if (s->curve_type == 1) { /* линия */
        if (n < cap) {
          pt[n].x = s->first_endpoint.x;
          pt[n].y = s->first_endpoint.y;
          n++;
        }
        last.x = s->second_endpoint.x;
        last.y = s->second_endpoint.y;
        have = 1;
      } else if (s->curve_type == 2) { /* дуга */
        MPt p1 = hatch_arc_pt(s->center.x, s->center.y, s->radius, 1.0,
                              s->start_angle);
        MPt p2 = hatch_arc_pt(s->center.x, s->center.y, s->radius, -1.0,
                              s->start_angle);
        double sgn = hatch_pick_sign(p1, p2, last, have, s->is_ccw);
        int m = hatch_arc_add(s->center.x, s->center.y, s->radius,
                              s->start_angle, s->end_angle, sgn, 0,
                              pt + n, cap - n);
        n += m;
        if (m > 0 && n > 0) { last = pt[n - 1]; have = 1; }
      } else if (s->curve_type == 3) { /* эллиптическая дуга */
        double cxx = s->center.x, cyy = s->center.y;
        double ex = s->endpoint.x - cxx, ey = s->endpoint.y - cyy;
        MPt p1 = hatch_ell_pt(cxx, cyy, ex, ey, s->minor_major_ratio, 1.0,
                              s->start_angle);
        MPt p2 = hatch_ell_pt(cxx, cyy, ex, ey, s->minor_major_ratio, -1.0,
                              s->start_angle);
        double sgn = hatch_pick_sign(p1, p2, last, have, s->is_ccw);
        int m = hatch_ell_add(cxx, cyy, ex, ey, s->minor_major_ratio,
                              s->start_angle, s->end_angle, sgn, 0,
                              pt + n, cap - n);
        n += m;
        if (m > 0 && n > 0) { last = pt[n - 1]; have = 1; }
      } else if (s->curve_type == 4 &&
                 s->control_points) { /* сплайн — по контрольным точкам */
        unsigned long m = s->num_control_points;
        if (m > 64)
          m = 64;
        for (unsigned long j = 0; j < m && n < cap; j++) {
          pt[n].x = s->control_points[j].point.x;
          pt[n].y = s->control_points[j].point.y;
          n++;
        }
        if (n > 0) { last = pt[n - 1]; have = 1; }
      }
    }
  }
  *out_n = n;
  return pt;
}

static void entity_hatch(MBlock *b, Dwg_Data *dwg, Dwg_Entity_HATCH *h,
                         COLORREF color, int pen)
{
  (void)dwg;
  if (!h || !h->paths || h->num_paths == 0)
    return;
  unsigned long np = h->num_paths;
  if (np > 10000)
    np = 10000;

  /* 1) заливка: сплошная — как есть; узорная — штриховым брашем */
  {
    MPt *pts = NULL;
    int n = 0;
    for (unsigned long i = 0; i < np && !pts; i++) {
      int cnt = 0;
      MPt *pp = hatch_loop_pts(&h->paths[i], &cnt);
      if (pp && cnt >= 3) {
        pts = pp;
        n = cnt;
      } else if (pp) {
        free(pp);
      }
    }
    if (pts && n >= 3) {
      int style = 1; /* сплошная заливка */
      if (!h->is_solid_fill) {
        double ang = (h->deflines && h->num_deflines) ? h->deflines[0].angle
                                                      : 45.0;
        double a = fmod(fabs(ang) * 180.0 / M_PI, 180.0);
        if (a < 22.5 || a >= 157.5)
          style = 4; /* горизонтальный узор */
        else if (a < 67.5)
          style = 2; /* 45° */
        else if (a < 112.5)
          style = 5; /* вертикальный */
        else
          style = 3; /* 135° */
        if (h->double_flag)
          style = 6; /* перекрестный */
      }
      emit_polygon(b, pts, n, color, DPS_SOLID, style);
      free(pts);
    }
  }

  /* 2) контуры петель */
  for (unsigned long i = 0; i < np; i++) {
    Dwg_HATCH_Path *path = &h->paths[i];
    if (path->flag & 2)
      hatch_path_polyline(b, path, color, pen);
    else
      hatch_path_segs(b, path, color, pen);
  }
}

static void host_3dsolid_wire(MBlock *b, Dwg_3DSOLID_wire *w, COLORREF color,
                              int pen)
{
  unsigned long n = w->num_points;
  if (!w->points || n < 2)
    return;
  if (n > 100000)
    n = 100000;
  MPt *pts = (MPt *)malloc((size_t)n * sizeof(MPt));
  if (!pts)
    return;
  for (unsigned long i = 0; i < n; i++) {
    pts[i].x = w->points[i].x;
    pts[i].y = w->points[i].y;
  }
  emit_polygon(b, pts, (int)n, color, pen, 0);
  free(pts);
}

static void entity_3dsolid(MBlock *b, Dwg_Entity__3DSOLID *s, COLORREF color,
                           int pen)
{
  if (!s)
    return;
  unsigned long nw = s->num_wires;
  if (s->wires && nw) {
    if (nw > 20000)
      nw = 20000;
    for (unsigned long i = 0; i < nw; i++)
      host_3dsolid_wire(b, &s->wires[i], color, pen);
  }
  unsigned long ns = s->num_silhouettes;
  if (s->silhouettes && ns) {
    if (ns > 2000)
      ns = 2000;
    for (unsigned long i = 0; i < ns; i++) {
      Dwg_3DSOLID_silhouette *sh = &s->silhouettes[i];
      unsigned long nw2 = sh->num_wires;
      if (!sh->wires || !nw2)
        continue;
      if (nw2 > 20000)
        nw2 = 20000;
      for (unsigned long j = 0; j < nw2; j++)
        host_3dsolid_wire(b, &sh->wires[j], color, pen);
    }
  }
}

static void entity_image(MBlock *b, Dwg_Entity_IMAGE *im, COLORREF color,
                         int pen)
{
  if (!im)
    return;
  double x0 = im->pt0.x, y0 = im->pt0.y;
  double ux = im->uvec.x, uy = im->uvec.y;
  double vx = im->vvec.x, vy = im->vvec.y;
  if (!is_fin(x0 + y0 + ux + uy + vx + vy))
    return;
  emit_line(b, x0, y0, x0 + ux, y0 + uy, color, pen);
  emit_line(b, x0 + ux, y0 + uy, x0 + ux + vx, y0 + uy + vy, color, pen);
  emit_line(b, x0 + ux + vx, y0 + uy + vy, x0 + vx, y0 + vy, color, pen);
  emit_line(b, x0 + vx, y0 + vy, x0, y0, color, pen);
}

/* ------------------------------------------------------------------ */
/* block building                                                      */

typedef struct {
  DwgModel *m;
  Dwg_Data *dwg;
  int blk;
  int depth;
} BuildCtx;

static void handle_insert(MBlock *b, const Dwg_Entity_INSERT *ins,
                          Dwg_Data *dwg, DwgModel *m)
{
  if (!ins || !ins->block_header)
    return;
  Dwg_Object *tobj = dwg_ref_object(dwg, ins->block_header);
  int target = -1;
  if (tobj) {
    for (int i = 0; i < m->n_blocks; i++) {
      if (m->blocks[i].key == (const void *)tobj) {
        target = i;
        break;
      }
    }
  }
  MInsert *r = insert_append(b);
  if (!r)
    return;
  r->pos.x = ins->ins_pt.x;
  r->pos.y = ins->ins_pt.y;
  r->scale[0] = fabs(ins->scale.x) < 1e-12 ? 1.0 : ins->scale.x;
  r->scale[1] = fabs(ins->scale.y) < 1e-12 ? 1.0 : ins->scale.y;
  r->rot = ins->rotation;
  r->target = target;
  /* guard insane values */
  if (!is_fin(r->pos.x) || !is_fin(r->pos.y) || !is_fin(r->rot))
    r->target = -1;
  if (!is_fin(r->scale[0]) || !is_fin(r->scale[1]))
    r->target = -1;
}

/* emit everything owned by this block header (or entity) into m->blocks[blk] */
static void build_canvas(BuildCtx *ctx, Dwg_Object *base, int is_block)
{
  DwgModel *m = ctx->m;
  MBlock *b = &m->blocks[ctx->blk];
  Dwg_Object *obj = NULL;
  if (is_block) {
    obj = get_first_owned_entity(base);
  }
  int guard = 0;
  while (obj && guard++ < 2000000) {
    if (obj->supertype == DWG_SUPERTYPE_ENTITY) {
      Dwg_Object_Entity *ent = obj->tio.entity;
      if (layer_is_off(ctx->dwg, ent->layer))
        goto next_owned;
      COLORREF color = ent_color(ctx->dwg, ent);
      int pen = ent_pen_style(ctx->dwg, ent);
      int fixed = obj->fixedtype;
      switch (fixed) {
      case DWG_TYPE_LINE:
        entity_line(b, ctx->dwg, ent->tio.LINE, color, pen);
        break;
      case DWG_TYPE_CIRCLE:
        entity_circle(b, ent->tio.CIRCLE, color, pen);
        break;
      case DWG_TYPE_ARC:
        entity_arc(b, ent->tio.ARC, color, pen);
        break;
      case DWG_TYPE_ELLIPSE:
        entity_ellipse(b, ent->tio.ELLIPSE, color, pen);
        break;
      case DWG_TYPE_LWPOLYLINE:
        entity_lwpolyline(b, ctx->dwg, ent->tio.LWPOLYLINE, color, pen);
        break;
      case DWG_TYPE_POLYLINE_2D:
        if (ent->tio.POLYLINE_2D)
          entity_polyline(b, ctx->dwg, ent->tio.POLYLINE_2D->vertex,
                          ent->tio.POLYLINE_2D->num_owned,
                          (ent->tio.POLYLINE_2D->flag & 1) != 0, color, pen);
        break;
      case DWG_TYPE_POLYLINE_3D:
        if (ent->tio.POLYLINE_3D)
          entity_polyline(
              b, ctx->dwg,
              (Dwg_Object_Ref **)(void *)ent->tio.POLYLINE_3D->vertex,
              ent->tio.POLYLINE_3D->num_owned,
              (ent->tio.POLYLINE_3D->flag & 1) != 0, color, pen);
        break;
      case DWG_TYPE_TEXT:
        entity_text(b, ctx->dwg, ent->tio.TEXT, color);
        break;
      case DWG_TYPE_MTEXT:
        entity_mtext(b, ctx->dwg, ent->tio.MTEXT, color);
        break;
      case DWG_TYPE_SPLINE:
        emit_spline(b, ent->tio.SPLINE, color, pen);
        break;
      case DWG_TYPE_POINT:
        emit_point(b, ent->tio.POINT->x, ent->tio.POINT->y, color);
        break;
      case DWG_TYPE_LEADER:
        entity_leader(b, ent->tio.LEADER, color, pen);
        break;
      case DWG_TYPE_SOLID:
      case DWG_TYPE_TRACE: {
        Dwg_Entity_SOLID *s =
            (Dwg_Entity_SOLID *)(fixed == DWG_TYPE_SOLID ? (void *)ent->tio.SOLID
                                                         : (void *)ent->tio.TRACE);
        if (s) {
          MPt pts[4];
          pts[0].x = s->corner1.x; pts[0].y = s->corner1.y;
          pts[1].x = s->corner2.x; pts[1].y = s->corner2.y;
          pts[2].x = s->corner3.x; pts[2].y = s->corner3.y;
          pts[3].x = s->corner4.x; pts[3].y = s->corner4.y;
          emit_polygon(b, pts, 4, color, pen, 1);
        }
        break;
      }
      case DWG_TYPE__3DFACE: {
        Dwg_Entity__3DFACE *f = ent->tio._3DFACE;
        if (f) {
          MPt pts[4];
          pts[0].x = f->corner1.x; pts[0].y = f->corner1.y;
          pts[1].x = f->corner2.x; pts[1].y = f->corner2.y;
          pts[2].x = f->corner3.x; pts[2].y = f->corner3.y;
          pts[3].x = f->corner4.x; pts[3].y = f->corner4.y;
          emit_polygon(b, pts, 4, color, pen, 1);
        }
        break;
      }
      case DWG_TYPE_INSERT:
        handle_insert(b, ent->tio.INSERT, ctx->dwg, m);
        break;
      case DWG_TYPE_MINSERT: {
        Dwg_Entity_MINSERT *mi = ent->tio.MINSERT;
        if (mi) {
          int rows = mi->num_rows ? mi->num_rows : 1;
          int cols = mi->num_cols ? mi->num_cols : 1;
          if (rows > 100)
            rows = 100;
          if (cols > 100)
            cols = 100;
          for (int r = 0; r < rows; r++) {
            for (int c = 0; c < cols; c++) {
              Dwg_Entity_INSERT tmp;
              memset(&tmp, 0, sizeof(tmp));
              tmp.ins_pt.x = mi->ins_pt.x + c * mi->col_spacing;
              tmp.ins_pt.y = mi->ins_pt.y + r * mi->row_spacing;
              tmp.scale = mi->scale;
              tmp.rotation = mi->rotation;
              tmp.block_header = mi->block_header;
              handle_insert(b, &tmp, ctx->dwg, m);
            }
          }
        }
        break;
      }
      case DWG_TYPE_DIMENSION_LINEAR:
      case DWG_TYPE_DIMENSION_ALIGNED:
      case DWG_TYPE_DIMENSION_ANG2LN:
      case DWG_TYPE_DIMENSION_ANG3PT:
      case DWG_TYPE_DIMENSION_DIAMETER:
      case DWG_TYPE_DIMENSION_RADIUS:
      case DWG_TYPE_DIMENSION_ORDINATE:
        if (ent->tio.DIMENSION_common)
          entity_dimension(b, ctx->dwg, ctx->m, ent->tio.DIMENSION_common,
                           color);
        break;
      case DWG_TYPE_MULTILEADER:
        entity_multileader(b, ctx->dwg, ctx->m, ent->tio.MULTILEADER, color,
                           pen);
        break;
      case DWG_TYPE_ATTRIB: {
        Dwg_Entity_ATTRIB *at = ent->tio.ATTRIB;
        if (at) {
          MPt pos = { at->ins_pt.x, at->ins_pt.y };
          emit_text(b, pos, at->height, at->rotation, at->text_value, color);
        }
        break;
      }
      case DWG_TYPE_HATCH:
        entity_hatch(b, ctx->dwg, ent->tio.HATCH, color, pen);
        break;
      case DWG_TYPE__3DSOLID:
      case DWG_TYPE_REGION:
      case DWG_TYPE_BODY:
        entity_3dsolid(b, ent->tio._3DSOLID, color, pen);
        break;
      case DWG_TYPE_IMAGE:
        entity_image(b, ent->tio.IMAGE, color, pen);
        break;
      case DWG_TYPE_VIEWPORT:
      case DWG_TYPE_ATTDEF:
      case DWG_TYPE_SHAPE:
      case DWG_TYPE_WIPEOUT:
      case DWG_TYPE_MLINE:
        /* not rendered in v1 */
        break;
      default:
        break;
      }
    }
    next_owned:;
    Dwg_Object *next = is_block ? get_next_owned_entity(base, obj) : NULL;
    if (next == obj)
      break;
    obj = next;
  }
}

/* ------------------------------------------------------------------ */
/* extents                                                             */

static void ext_include(XForm *xf, double x, double y, DwgModel *m)
{
  double dx, dy;
  xf_apply(xf, x, y, &dx, &dy);
  if (dx < m->xmin)
    m->xmin = dx;
  if (dx > m->xmax)
    m->xmax = dx;
  if (dy < m->ymin)
    m->ymin = dy;
  if (dy > m->ymax)
    m->ymax = dy;
}

static void block_extents(DwgModel *m, int bi, XForm *xf, int depth)
{
  if (bi < 0 || bi >= m->n_blocks || depth > 50)
    return;
  MBlock *b = &m->blocks[bi];
  for (int i = 0; i < b->n_prims; i++) {
    KPrim *p = &b->prims[i];
    if (p->kind == KP_TEXT) {
      ext_include(xf, p->pos.x, p->pos.y, m);
      ext_include(xf, p->pos.x + p->height, p->pos.y, m);
      ext_include(xf, p->pos.x, p->pos.y + p->height, m);
    } else {
      for (int j = 0; j < p->n; j++)
        ext_include(xf, p->p[j].x, p->p[j].y, m);
    }
  }
  for (int i = 0; i < b->n_inserts; i++) {
    MInsert *ir = &b->inserts[i];
    if (ir->target < 0)
      continue;
    XForm t;
    xf_identity(&t);
    xf_scale(&t, ir->scale[0], ir->scale[1]);
    xf_rotate(&t, ir->rot);
    xf_translate(&t, ir->pos.x, ir->pos.y);
    xf_mul(&t, xf); /* t = xf ∘ (T∘R∘S) */
    block_extents(m, ir->target, &t, depth + 1);
  }
}

/* ------------------------------------------------------------------ */
/* load                                                                */

static long long block_content(const DwgModel *mm, int i)
{
  if (i < 0 || i >= mm->n_blocks)
    return 0;
  return (long long)mm->blocks[i].n_prims +
         (long long)mm->blocks[i].n_inserts;
}

static void pick_render_block(DwgModel *m, int ms_idx, int ps_idx)
{
  int pick = ms_idx >= 0 ? ms_idx : ps_idx;
  if (pick < 0 && m->n_blocks > 0)
    pick = 0;
  if (pick >= 0 && block_content(m, pick) == 0) {
    if (ps_idx >= 0 && block_content(m, ps_idx) > 0)
      pick = ps_idx;
    else {
      /* choose the block with the most content */
      int bestb = pick;
      long long best = block_content(m, bestb);
      for (int i = 0; i < m->n_blocks; i++) {
        long long c = block_content(m, i);
        if (c > best) {
          best = c;
          bestb = i;
        }
      }
      pick = bestb;
    }
  }
  m->render_block = pick;
}

static int read_dwg(const wchar_t *path, Dwg_Data *dwg)
{
  int err = DWG_ERR_CRITICAL;
  char ansi[4096];
  memset(dwg, 0, sizeof(*dwg));
  int n = WideCharToMultiByte(CP_ACP, 0, path, -1, ansi, sizeof(ansi),
                              NULL, NULL);
  if (n > 0) {
    err = dwg_read_file(ansi, dwg);
    if (err < DWG_ERR_CRITICAL)
      return err;
  }
  /* fallback: copy to a temporary ASCII path */
  wchar_t tmpdir[MAX_PATH];
  wchar_t tmpfile[MAX_PATH];
  if (!GetTempPathW(MAX_PATH, tmpdir))
    return err;
  _snwprintf(tmpfile, MAX_PATH, L"%sdwgviewer_%lu_%lu.dwg", tmpdir,
             (unsigned long)GetCurrentProcessId(),
             (unsigned long)GetTickCount());
  FILE *src = _wfopen(path, L"rb");
  if (!src)
    return err;
  FILE *dst = _wfopen(tmpfile, L"wb");
  if (!dst) { fclose(src); return err; }
  char buf[65536];
  size_t rd;
  while ((rd = fread(buf, 1, sizeof(buf), src)) > 0)
    fwrite(buf, 1, rd, dst);
  fclose(src);
  fclose(dst);
  char ascii[MAX_PATH];
  WideCharToMultiByte(CP_ACP, 0, tmpfile, -1, ascii, MAX_PATH, NULL, NULL);
  memset(dwg, 0, sizeof(*dwg));
  err = dwg_read_file(ascii, dwg);
  _wremove(tmpfile);
  return err;
}

DwgModel *model_load(const wchar_t *path, wchar_t *errbuf, int errlen)
{
  if (errbuf && errlen > 0)
    errbuf[0] = 0;
  DwgModel *m = (DwgModel *)calloc(1, sizeof(DwgModel));
  if (!m)
    return NULL;
  m->render_block = -1;

  Dwg_Data *dwg = &m->dwg;
  int err = read_dwg(path, dwg);
  if (err >= DWG_ERR_CRITICAL) {
    /* still build an empty model with a message */
    if (errbuf) {
      const wchar_t *lerr = dwg_lib_error();
      if (lerr) {
        _snwprintf(errbuf, errlen, L"%ls", lerr);
      } else {
        _snwprintf(errbuf, errlen,
                   L"Не удалось прочитать DWG файл (код ошибки %d).\n"
                   L"Файл может быть повреждён, зашифрован или имеет неподдерживаемую версию.",
                   err);
      }
    }
    model_free(m);
    return NULL;
  }
  snprintf(m->version, sizeof(m->version), "%s",
           dwg_version_type(dwg->header.version));
  m->obj_count = dwg->num_objects;

  /* ---- build one canvas per block header ---- */
  int nblocks = 0;
  for (unsigned int i = 0; i < dwg->num_objects; i++) {
    Dwg_Object *obj = &dwg->object[i];
    if (obj->supertype == DWG_SUPERTYPE_OBJECT &&
        obj->fixedtype == DWG_TYPE_BLOCK_HEADER)
      nblocks++;
  }
  if (nblocks == 0) {
    if (errbuf && errlen > 0)
      _snwprintf(errbuf, errlen,
                 L"В файле не найдено ни одного блока (возможно, "
                 L"неподдерживаемая/повреждённая версия DWG)");
    model_free(m);
    return NULL;
  }
  m->n_blocks = nblocks;
  m->blocks = (MBlock *)calloc((size_t)nblocks, sizeof(MBlock));
  if (!m->blocks) {
    if (errbuf && errlen > 0)
      _snwprintf(errbuf, errlen, L"Недостаточно памяти");
    model_free(m);
    return NULL;
  }
  /* Проход 1: сначала назначаем ключи ВСЕМ блокам, чтобы INSERT мог ссылаться
     на блок, определённый позже (иначе «вперёд»-ссылки не разрешаются и
     содержимое блоков не выводится). */
  int bi = 0;
  for (unsigned int i = 0; i < dwg->num_objects; i++) {
    Dwg_Object *obj = &dwg->object[i];
    if (obj->supertype != DWG_SUPERTYPE_OBJECT ||
        obj->fixedtype != DWG_TYPE_BLOCK_HEADER)
      continue;
    m->blocks[bi].id = bi;
    m->blocks[bi].key = (const void *)obj;
    bi++;
  }
  /* Проход 2: строим содержимое блоков (теперь все ключи уже проставлены). */
  BuildCtx ctx = { m, dwg, 0, 0 };
  bi = 0;
  for (unsigned int i = 0; i < dwg->num_objects; i++) {
    Dwg_Object *obj = &dwg->object[i];
    if (obj->supertype != DWG_SUPERTYPE_OBJECT ||
        obj->fixedtype != DWG_TYPE_BLOCK_HEADER)
      continue;
    ctx.blk = bi;
    build_canvas(&ctx, obj, 1);
    bi++;
  }

  /* ---- resolve insets targets (done at emit time; nothing to do here) ---- */

  /* ---- choose render space ---- */
  Dwg_Object_Ref *msref = dwg_model_space_ref(dwg);
  Dwg_Object_Ref *psref = dwg_paper_space_ref(dwg);
  Dwg_Object *ms = msref ? dwg_ref_object(dwg, msref) : NULL;
  Dwg_Object *ps = psref ? dwg_ref_object(dwg, psref) : NULL;
  int ms_idx = -1, ps_idx = -1;
  for (int i = 0; i < m->n_blocks; i++) {
    if (ms && m->blocks[i].key == (const void *)ms)
      ms_idx = i;
    if (ps && m->blocks[i].key == (const void *)ps)
      ps_idx = i;
  }
  pick_render_block(m, ms_idx, ps_idx);

  /* ---- extents over the rendered space (incl. nested inserts) ---- */
  m->xmin = m->ymin = 1e100;
  m->xmax = m->ymax = -1e100;
  if (m->render_block >= 0) {
    XForm id;
    xf_identity(&id);
    block_extents(m, m->render_block, &id, 0);
    if (m->xmin < m->xmax && m->ymin < m->ymax)
      m->has_content = 1;
    /* guard degenerate extents */
    if (m->xmax - m->xmin < 1e-12)
      m->has_content = 0;
  }
  return m;
}

void model_free(DwgModel *m)
{
  if (!m)
    return;
  for (int i = 0; i < m->n_blocks; i++) {
    MBlock *b = &m->blocks[i];
    for (int j = 0; j < b->n_prims; j++) {
      if (b->prims[j].text)
        free(b->prims[j].text);
      if (b->prims[j].p)
        free(b->prims[j].p);
    }
    free(b->prims);
    free(b->inserts);
  }
  free(m->blocks);
  dwg_free(&m->dwg);
  free(m);
}