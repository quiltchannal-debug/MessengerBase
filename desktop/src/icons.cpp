// OrangeM Desktop - реализация набора иконок (фигуры аналогичны ICON_PATHS веб-клиента).
// Все иконки рисуются контуром (перо 1.6 при 18px), заливка только у dots / crown / точек-маркеров.
#include "icons.h"

#include <algorithm>
#include <cmath>
#include <string>

namespace om {
namespace ui {

namespace {

using namespace Gdiplus;

const float kPi = 3.14159265358979f;

// Вспомогательный рисовальщик в системе координат 24x24 с масштабированием в size.
struct P {
  Graphics& g;
  float s, ox, oy, pw;
  Color col;
  PointF cur;

  P(Graphics& gg, int x, int y, int size, Color c)
      : g(gg), s((float)(size > 0 ? size : 1) / 24.0f), ox((float)x), oy((float)y),
        pw(1.6f * (float)(size > 0 ? size : 1) / 18.0f), col(c), cur(0.0f, 0.0f) {}

  PointF pt(float x, float y) const { return PointF(ox + x * s, oy + y * s); }
  RectF bx(float x, float y, float w, float h) const {
    return RectF(ox + x * s, oy + y * s, w * s, h * s);
  }

  // Новая фигура (команда M в SVG).
  void mv(GraphicsPath& p, float x, float y) {
    p.StartFigure();
    cur = pt(x, y);
  }
  // Линия к точке (команда L).
  void ln(GraphicsPath& p, float x, float y) {
    p.AddLine(cur, pt(x, y));
    cur = pt(x, y);
  }
  // Кубическая кривая (команда C) от текущей точки.
  void bez(GraphicsPath& p, float c1x, float c1y, float c2x, float c2y, float x2, float y2) {
    p.AddBezier(cur, pt(c1x, c1y), pt(c2x, c2y), pt(x2, y2));
    cur = pt(x2, y2);
  }
  // Эллипс отдельной фигурой.
  void ell(GraphicsPath& p, float cx, float cy, float rx, float ry) {
    p.StartFigure();
    p.AddEllipse(bx(cx - rx, cy - ry, rx * 2.0f, ry * 2.0f));
    cur = pt(cx, cy);
  }
  // Скруглённый прямоугольник отдельной фигурой.
  void rr(GraphicsPath& p, float x, float y, float w, float h, float r) {
    float d = r * 2.0f;
    if (d > w) d = w;
    if (d > h) d = h;
    p.StartFigure();
    p.AddArc(bx(x, y, d, d), 180.0f, 90.0f);
    p.AddArc(bx(x + w - d, y, d, d), 270.0f, 90.0f);
    p.AddArc(bx(x + w - d, y + h - d, d, d), 0.0f, 90.0f);
    p.AddArc(bx(x, y + h - d, d, d), 90.0f, 90.0f);
    p.CloseFigure();
    cur = pt(x + d * 0.5f, y);
  }
  // Дуга окружности от текущей точки к (x2, y2) радиусом r (команда A в SVG).
  void arc(GraphicsPath& p, float x2, float y2, float r, bool large, bool cw) {
    float x1 = (cur.X - ox) / s, y1 = (cur.Y - oy) / s;
    float dx = x2 - x1, dy = y2 - y1;
    float d = sqrtf(dx * dx + dy * dy);
    if (d < 0.001f) return;
    if (r < d * 0.5f) r = d * 0.5f;
    float hh = sqrtf(std::max(0.0f, r * r - d * d * 0.25f));
    float mx = (x1 + x2) * 0.5f, my = (y1 + y2) * 0.5f;
    float ux = -dy / d, uy = dx / d;
    for (int i = 0; i < 2; i++) {
      float sx = (i == 0) ? ux : -ux, sy = (i == 0) ? uy : -uy;
      float cx = mx + sx * hh, cy = my + sy * hh;
      float a1 = atan2f(y1 - cy, x1 - cx) * 180.0f / kPi;
      float a2 = atan2f(y2 - cy, x2 - cx) * 180.0f / kPi;
      float sw = a2 - a1;
      while (sw <= -180.0f) sw += 360.0f;
      while (sw > 180.0f) sw -= 360.0f;
      if ((sw > 0.0f) != cw) sw += (sw > 0.0f ? -360.0f : 360.0f);
      float mag = fabsf(sw);
      if (mag > 180.5f && !large) continue;
      if (mag < 179.5f && large) continue;
      p.AddArc(bx(cx - r, cy - r, r * 2.0f, r * 2.0f), a1, sw);
      cur = pt(x2, y2);
      return;
    }
    p.AddLine(cur, pt(x2, y2));
    cur = pt(x2, y2);
  }
  void end(GraphicsPath& p) { p.CloseFigure(); }

  // Обводка (или заливка) накопленного пути.
  void draw(GraphicsPath& p, bool fill = false) {
    if (p.GetPointCount() == 0) return;
    if (fill) {
      SolidBrush b(col);
      g.FillPath(&b, &p);
    }
    Pen pen(col, pw);
    pen.SetLineCap(LineCapRound, LineCapRound, DashCapRound);
    pen.SetLineJoin(LineJoinRound);
    g.DrawPath(&pen, &p);
  }
  // Залитая точка.
  void dot(float cx, float cy, float r) {
    GraphicsPath p;
    p.AddEllipse(bx(cx - r, cy - r, r * 2.0f, r * 2.0f));
    SolidBrush b(col);
    g.FillPath(&b, &p);
  }
};

// Рисует одну иконку; возвращает false, если имя неизвестно.
bool drawNamed(Graphics& g, const std::string& name, int x, int y, int size, Color color) {
  P q(g, x, y, size, color);
  GraphicsPath p;

  if (name == "home") {
    q.mv(p, 3, 10.6f); q.ln(p, 12, 3); q.ln(p, 21, 10.6f);
    q.mv(p, 5.5f, 9.4f); q.ln(p, 5.5f, 21); q.ln(p, 18.5f, 21); q.ln(p, 18.5f, 9.4f);
    q.mv(p, 9.8f, 21); q.ln(p, 9.8f, 14.8f); q.ln(p, 14.2f, 14.8f); q.ln(p, 14.2f, 21);
    q.draw(p);
  } else if (name == "chat") {
    q.mv(p, 21, 11.6f);
    q.bez(p, 21, 15.8f, 17, 19.2f, 12, 19.2f);
    q.bez(p, 11, 19.2f, 10, 19.06f, 9.1f, 18.8f);
    q.ln(p, 4, 21); q.ln(p, 5.3f, 17.4f);
    q.bez(p, 4.1f, 16.2f, 3, 14, 3, 11.6f);
    q.bez(p, 3, 7.4f, 7, 4, 12, 4);
    q.bez(p, 17, 4, 21, 7.4f, 21, 11.6f);
    q.end(p);
    q.draw(p);
  } else if (name == "users") {
    q.mv(p, 16.5f, 20); q.ln(p, 16.5f, 18.4f);
    q.arc(p, 12.6f, 14.5f, 3.9f, false, false);
    q.ln(p, 7.4f, 14.5f);
    q.arc(p, 3.5f, 18.4f, 3.9f, false, false);
    q.ln(p, 3.5f, 20);
    q.ell(p, 10, 7.6f, 3.6f, 3.6f);
    q.mv(p, 20.5f, 20); q.ln(p, 20.5f, 18.4f);
    q.arc(p, 17.6f, 14.6f, 3.9f, false, false);
    q.mv(p, 15.5f, 4.2f);
    q.arc(p, 15.5f, 11.2f, 3.6f, false, true);
    q.draw(p);
  } else if (name == "stories") {
    q.ell(p, 12, 12, 8.5f, 8.5f);
    q.ell(p, 12, 12, 3.2f, 3.2f);
    q.draw(p);
  } else if (name == "settings") {
    q.ell(p, 12, 12, 3.1f, 3.1f);
    q.draw(p);
    GraphicsPath t;
    q.ell(t, 12, 12, 7.3f, 7.3f);
    for (int i = 0; i < 8; i++) {
      float a = (float)i * 45.0f * kPi / 180.0f;
      float c = cosf(a), sn = sinf(a);
      q.mv(t, 12 + 7.3f * c, 12 + 7.3f * sn);
      q.ln(t, 12 + 9.7f * c, 12 + 9.7f * sn);
    }
    q.draw(t);
  } else if (name == "search") {
    q.ell(p, 10.8f, 10.8f, 7.2f, 7.2f);
    q.mv(p, 16.2f, 16.2f); q.ln(p, 21, 21);
    q.draw(p);
  } else if (name == "send") {
    q.mv(p, 21.5f, 2.5f); q.ln(p, 2.8f, 10.2f); q.ln(p, 10, 13.1f); q.ln(p, 12.9f, 20.3f);
    q.end(p);
    q.mv(p, 21.5f, 2.5f); q.ln(p, 10, 12.9f);
    q.draw(p);
  } else if (name == "attach") {
    q.mv(p, 20.4f, 11.6f); q.ln(p, 12, 20);
    q.arc(p, 4.6f, 12.6f, 5.2f, false, true);
    q.ln(p, 13.3f, 3.9f);
    q.arc(p, 18.2f, 8.8f, 3.5f, false, true);
    q.ln(p, 9.5f, 17.5f);
    q.arc(p, 7, 15, 1.7f, false, true);
    q.ln(p, 15, 7);
    q.draw(p);
  } else if (name == "image") {
    q.rr(p, 3, 4.5f, 18, 15, 2);
    q.ell(p, 8.6f, 9.6f, 1.6f, 1.6f);
    q.mv(p, 3.5f, 16.8f); q.ln(p, 9, 12); q.ln(p, 12.5f, 15); q.ln(p, 15.5f, 12.5f);
    q.ln(p, 20.5f, 17);
    q.draw(p);
  } else if (name == "video") {
    q.rr(p, 2.5f, 5.5f, 13, 13, 2);
    q.mv(p, 15.5f, 10.5f); q.ln(p, 21.5f, 7); q.ln(p, 21.5f, 17); q.ln(p, 15.5f, 13.5f);
    q.end(p);
    q.draw(p);
  } else if (name == "music") {
    q.mv(p, 9, 18); q.ln(p, 9, 6.5f); q.ln(p, 19, 4.5f); q.ln(p, 19, 15.5f);
    q.ell(p, 6.5f, 18, 2.5f, 2.5f);
    q.ell(p, 16.5f, 15.5f, 2.5f, 2.5f);
    q.draw(p);
  } else if (name == "file") {
    q.mv(p, 14, 3); q.ln(p, 7, 3);
    q.arc(p, 5, 5, 2, false, false);
    q.ln(p, 5, 19);
    q.arc(p, 7, 21, 2, false, false);
    q.ln(p, 17, 21);
    q.arc(p, 19, 19, 2, false, false);
    q.ln(p, 19, 8); q.ln(p, 14, 3);
    q.end(p);
    q.mv(p, 14, 3); q.ln(p, 14, 8); q.ln(p, 19, 8);
    q.draw(p);
  } else if (name == "heart") {
    q.mv(p, 12, 20.3f); q.ln(p, 4.9f, 13.2f);
    q.arc(p, 11.4f, 6.7f, 4.6f, true, true);
    q.ln(p, 12, 7.3f); q.ln(p, 12.6f, 6.7f);
    q.arc(p, 19.1f, 13.2f, 4.6f, true, true);
    q.end(p);
    q.draw(p);
  } else if (name == "comment") {
    q.mv(p, 21, 11.5f);
    q.bez(p, 21, 15.5f, 17, 18.7f, 12, 18.7f);
    q.bez(p, 11, 18.7f, 10, 18.57f, 9.1f, 18.33f);
    q.ln(p, 4, 21); q.ln(p, 5.3f, 17.5f);
    q.bez(p, 4.1f, 16, 3, 13.9f, 3, 11.5f);
    q.bez(p, 3, 7.6f, 7, 4.3f, 12, 4.3f);
    q.bez(p, 17, 4.3f, 21, 7.6f, 21, 11.5f);
    q.end(p);
    q.mv(p, 8.5f, 10.5f); q.ln(p, 15.5f, 10.5f);
    q.mv(p, 8.5f, 13.5f); q.ln(p, 12.5f, 13.5f);
    q.draw(p);
  } else if (name == "link") {
    q.mv(p, 10, 13.5f);
    q.arc(p, 15.7f, 13.5f, 4, false, false);
    q.ln(p, 18.7f, 10.5f);
    q.arc(p, 13, 10.5f, 4, false, false);
    q.ln(p, 11.8f, 11.7f);
    q.mv(p, 14, 10.5f);
    q.arc(p, 8.3f, 10.5f, 4, false, false);
    q.ln(p, 5.3f, 13.5f);
    q.arc(p, 11, 13.5f, 4, false, false);
    q.ln(p, 12.2f, 12.3f);
    q.draw(p);
  } else if (name == "trash") {
    q.mv(p, 4, 7); q.ln(p, 20, 7);
    q.mv(p, 9.5f, 7); q.ln(p, 9.5f, 4.8f); q.ln(p, 14.5f, 4.8f); q.ln(p, 14.5f, 7);
    q.mv(p, 6.5f, 7); q.ln(p, 7.5f, 20.2f); q.ln(p, 16.5f, 20.2f); q.ln(p, 17.5f, 7);
    q.mv(p, 10.5f, 11); q.ln(p, 10.5f, 16.5f);
    q.mv(p, 13.5f, 11); q.ln(p, 13.5f, 16.5f);
    q.draw(p);
  } else if (name == "edit") {
    q.mv(p, 4, 20); q.ln(p, 8, 20); q.ln(p, 18, 10);
    q.arc(p, 14, 6, 2.8f, false, false);
    q.ln(p, 4, 16); q.end(p);
    q.mv(p, 14.5f, 6.5f); q.ln(p, 17.5f, 9.5f);
    q.draw(p);
  } else if (name == "reply") {
    q.mv(p, 9, 8); q.ln(p, 6, 8); q.ln(p, 3, 11.5f); q.ln(p, 6, 15); q.ln(p, 9, 15);
    q.mv(p, 6, 11.5f); q.ln(p, 13.5f, 11.5f);
    q.arc(p, 18.5f, 16.5f, 5, false, true);
    q.ln(p, 18.5f, 19);
    q.draw(p);
  } else if (name == "react") {
    q.ell(p, 12, 12, 8.6f, 8.6f);
    q.mv(p, 9, 14.5f);
    q.arc(p, 15, 14.5f, 3.8f, false, false);
    q.draw(p);
    q.dot(9.2f, 9.6f, 0.75f);
    q.dot(14.8f, 9.6f, 0.75f);
  } else if (name == "plus") {
    q.mv(p, 12, 5); q.ln(p, 12, 19);
    q.mv(p, 5, 12); q.ln(p, 19, 12);
    q.draw(p);
  } else if (name == "close") {
    q.mv(p, 6, 6); q.ln(p, 18, 18);
    q.mv(p, 18, 6); q.ln(p, 6, 18);
    q.draw(p);
  } else if (name == "arrow-left") {
    q.mv(p, 20, 12); q.ln(p, 4, 12);
    q.mv(p, 10, 6); q.ln(p, 4, 12); q.ln(p, 10, 18);
    q.draw(p);
  } else if (name == "arrow-right") {
    q.mv(p, 4, 12); q.ln(p, 20, 12);
    q.mv(p, 14, 6); q.ln(p, 20, 12); q.ln(p, 14, 18);
    q.draw(p);
  } else if (name == "arrow-up") {
    q.mv(p, 12, 20); q.ln(p, 12, 4);
    q.mv(p, 6, 10); q.ln(p, 12, 4); q.ln(p, 18, 10);
    q.draw(p);
  } else if (name == "chevron-left") {
    q.mv(p, 15, 5); q.ln(p, 8, 12); q.ln(p, 15, 19);
    q.draw(p);
  } else if (name == "chevron-right") {
    q.mv(p, 9, 5); q.ln(p, 16, 12); q.ln(p, 9, 19);
    q.draw(p);
  } else if (name == "chevron-down") {
    q.mv(p, 5, 9); q.ln(p, 12, 16); q.ln(p, 19, 9);
    q.draw(p);
  } else if (name == "pin") {
    q.mv(p, 15.5f, 3.5f); q.ln(p, 20.5f, 8.5f); q.ln(p, 17.5f, 9.7f); q.ln(p, 13.9f, 13.3f);
    q.ln(p, 13.3f, 16.3f); q.ln(p, 10.7f, 13.7f); q.ln(p, 6.7f, 17.7f); q.ln(p, 5.4f, 16.4f);
    q.ln(p, 9.4f, 12.4f); q.ln(p, 6.8f, 9.8f); q.ln(p, 9.8f, 9.2f); q.ln(p, 13.4f, 5.6f);
    q.end(p);
    q.draw(p);
  } else if (name == "mute") {
    q.mv(p, 4, 9.5f); q.ln(p, 7, 9.5f); q.ln(p, 11, 6); q.ln(p, 11, 18); q.ln(p, 7, 14.5f);
    q.ln(p, 4, 14.5f); q.end(p);
    q.mv(p, 15, 9.5f); q.ln(p, 20, 15);
    q.mv(p, 20, 9.5f); q.ln(p, 15, 15);
    q.draw(p);
  } else if (name == "eye") {
    q.mv(p, 2.5f, 12);
    q.bez(p, 5.5f, 7.5f, 8.5f, 6.5f, 12, 6.5f);
    q.bez(p, 15.5f, 6.5f, 18.5f, 7.5f, 21.5f, 12);
    q.bez(p, 18.5f, 16.5f, 15.5f, 17.5f, 12, 17.5f);
    q.bez(p, 8.5f, 17.5f, 5.5f, 16.5f, 2.5f, 12);
    q.end(p);
    q.ell(p, 12, 12, 2.8f, 2.8f);
    q.draw(p);
  } else if (name == "lock") {
    q.rr(p, 4.8f, 10.5f, 14.4f, 10, 2);
    q.mv(p, 8.2f, 10.5f); q.ln(p, 8.2f, 8);
    q.arc(p, 15.8f, 8, 3.8f, false, true);
    q.ln(p, 15.8f, 10.5f);
    q.draw(p);
  } else if (name == "mail") {
    q.rr(p, 3, 5.5f, 18, 13, 2);
    q.mv(p, 3.6f, 7); q.ln(p, 12, 13); q.ln(p, 20.4f, 7);
    q.draw(p);
  } else if (name == "phone") {
    q.mv(p, 7.5f, 3.5f); q.ln(p, 10.5f, 3.5f); q.ln(p, 12, 7.5f); q.ln(p, 10, 9);
    q.arc(p, 15, 14, 11, false, false);
    q.ln(p, 16.5f, 12); q.ln(p, 20.5f, 13.5f); q.ln(p, 20.5f, 16.5f);
    q.arc(p, 18.3f, 18.5f, 2, false, true);
    q.arc(p, 5.5f, 5.7f, 16.5f, true, true);
    q.arc(p, 7.5f, 3.5f, 2, false, true);
    q.end(p);
    q.draw(p);
  } else if (name == "key") {
    q.ell(p, 8, 14, 3.5f, 3.5f);
    q.mv(p, 10.5f, 11.5f); q.ln(p, 19, 3);
    q.mv(p, 16, 6); q.ln(p, 18.5f, 8.5f);
    q.mv(p, 14, 8); q.ln(p, 16.5f, 10.5f);
    q.draw(p);
  } else if (name == "qr") {
    q.rr(p, 3.5f, 3.5f, 6.5f, 6.5f, 1);
    q.rr(p, 14, 3.5f, 6.5f, 6.5f, 1);
    q.rr(p, 3.5f, 14, 6.5f, 6.5f, 1);
    q.mv(p, 14, 14); q.ln(p, 17, 14); q.ln(p, 17, 17); q.ln(p, 14, 17); q.end(p);
    q.mv(p, 20.5f, 14); q.ln(p, 20.5f, 17);
    q.mv(p, 17.5f, 20.5f); q.ln(p, 20.5f, 20.5f);
    q.draw(p);
    q.dot(14, 20.5f, 0.6f);
  } else if (name == "shield") {
    q.mv(p, 12, 3); q.ln(p, 5, 5.8f); q.ln(p, 5, 11.2f);
    q.bez(p, 5, 15.5f, 8, 18.8f, 12, 21);
    q.bez(p, 16, 18.8f, 19, 15.5f, 19, 11.2f);
    q.ln(p, 19, 5.8f); q.end(p);
    q.draw(p);
  } else if (name == "copy") {
    q.rr(p, 9, 9, 11.5f, 11.5f, 2);
    q.mv(p, 15.5f, 6.2f); q.ln(p, 15.5f, 5);
    q.arc(p, 13.5f, 3, 2, false, false);
    q.ln(p, 5, 3);
    q.arc(p, 3, 5, 2, false, false);
    q.ln(p, 3, 13.5f);
    q.arc(p, 5, 15.5f, 2, false, false);
    q.ln(p, 6.2f, 15.5f);
    q.draw(p);
  } else if (name == "check") {
    q.mv(p, 4.5f, 12.5f); q.ln(p, 9.5f, 17.5f); q.ln(p, 19.5f, 6.5f);
    q.draw(p);
  } else if (name == "refresh") {
    q.mv(p, 20, 11.5f);
    q.arc(p, 6.3f, 17.7f, 8, true, true);
    q.ln(p, 4, 15.5f);
    q.mv(p, 4, 12.5f);
    q.arc(p, 17.7f, 6.3f, 8, true, true);
    q.ln(p, 20, 8.5f);
    q.mv(p, 20, 3.5f); q.ln(p, 20, 8.5f); q.ln(p, 15, 8.5f);
    q.mv(p, 4, 20.5f); q.ln(p, 4, 15.5f); q.ln(p, 9, 15.5f);
    q.draw(p);
  } else if (name == "logout") {
    q.mv(p, 15, 4.5f); q.ln(p, 18.5f, 4.5f);
    q.arc(p, 20.5f, 6.5f, 2, false, true);
    q.ln(p, 20.5f, 17.5f);
    q.arc(p, 18.5f, 19.5f, 2, false, true);
    q.ln(p, 15, 19.5f);
    q.mv(p, 11, 8); q.ln(p, 7, 12); q.ln(p, 11, 16);
    q.mv(p, 7, 12); q.ln(p, 16, 12);
    q.draw(p);
  } else if (name == "user") {
    q.ell(p, 12, 8, 3.8f, 3.8f);
    q.mv(p, 4.5f, 20.5f); q.ln(p, 4.5f, 19.3f);
    q.arc(p, 9.8f, 14, 5.3f, false, true);
    q.ln(p, 14.2f, 14);
    q.arc(p, 19.5f, 19.3f, 5.3f, false, true);
    q.ln(p, 19.5f, 20.5f);
    q.draw(p);
  } else if (name == "bell") {
    q.mv(p, 18, 15.5f); q.ln(p, 18, 11);
    q.arc(p, 6, 11, 6, true, false);
    q.ln(p, 6, 15.5f); q.ln(p, 4.5f, 18); q.ln(p, 19.5f, 18); q.end(p);
    q.mv(p, 9.8f, 21); q.ln(p, 14.2f, 21);
    q.draw(p);
  } else if (name == "chart") {
    q.mv(p, 4, 20.5f); q.ln(p, 20, 20.5f);
    q.mv(p, 7, 20.5f); q.ln(p, 7, 12);
    q.mv(p, 12, 20.5f); q.ln(p, 12, 5.5f);
    q.mv(p, 17, 20.5f); q.ln(p, 17, 15.5f);
    q.draw(p);
  } else if (name == "globe") {
    q.ell(p, 12, 12, 8.6f, 8.6f);
    q.mv(p, 3.4f, 12); q.ln(p, 20.6f, 12);
    q.mv(p, 12, 3.4f);
    q.bez(p, 14.4f, 6, 15.6f, 8.8f, 15.6f, 12);
    q.bez(p, 15.6f, 15.2f, 14.4f, 18, 12, 20.6f);
    q.bez(p, 9.6f, 18, 8.4f, 15.2f, 8.4f, 12);
    q.bez(p, 8.4f, 8.8f, 9.6f, 6, 12, 3.4f);
    q.end(p);
    q.draw(p);
  } else if (name == "dots") {
    q.dot(5.5f, 12, 1.5f);
    q.dot(12, 12, 1.5f);
    q.dot(18.5f, 12, 1.5f);
  } else if (name == "crown") {
    q.mv(p, 3.5f, 18.5f); q.ln(p, 20.5f, 18.5f); q.ln(p, 19, 7.5f); q.ln(p, 14.5f, 11.5f);
    q.ln(p, 12, 5.5f); q.ln(p, 9.5f, 11.5f); q.ln(p, 5, 7.5f);
    q.end(p);
    q.draw(p, true);
  } else if (name == "star") {
    q.mv(p, 12, 4); q.ln(p, 14.5f, 9.2f); q.ln(p, 20, 10); q.ln(p, 16, 13.9f);
    q.ln(p, 17, 19.4f); q.ln(p, 12, 16.7f); q.ln(p, 7, 19.4f); q.ln(p, 8, 13.9f);
    q.ln(p, 4, 10); q.ln(p, 9.5f, 9.2f);
    q.end(p);
    q.draw(p);
  } else if (name == "info") {
    q.ell(p, 12, 12, 8.6f, 8.6f);
    q.mv(p, 12, 11); q.ln(p, 12, 16.5f);
    q.draw(p);
    q.dot(12, 7.9f, 0.6f);
  } else if (name == "warning") {
    q.mv(p, 12, 4.5f); q.ln(p, 21, 19.5f); q.ln(p, 3, 19.5f); q.end(p);
    q.mv(p, 12, 10); q.ln(p, 12, 14);
    q.draw(p);
    q.dot(12, 16.6f, 0.6f);
  } else if (name == "clock") {
    q.ell(p, 12, 12, 8.6f, 8.6f);
    q.mv(p, 12, 7.5f); q.ln(p, 12, 12); q.ln(p, 15, 14);
    q.draw(p);
  } else if (name == "camera") {
    q.mv(p, 4, 7.5f); q.ln(p, 7, 7.5f); q.ln(p, 8.5f, 5); q.ln(p, 15.5f, 5);
    q.ln(p, 17, 7.5f); q.ln(p, 20, 7.5f);
    q.arc(p, 21.5f, 9, 1.5f, false, true);
    q.ln(p, 21.5f, 18);
    q.arc(p, 20, 19.5f, 1.5f, false, true);
    q.ln(p, 4, 19.5f);
    q.arc(p, 2.5f, 18, 1.5f, false, true);
    q.ln(p, 2.5f, 9);
    q.arc(p, 4, 7.5f, 1.5f, false, true);
    q.end(p);
    q.ell(p, 12, 13, 3.4f, 3.4f);
    q.draw(p);
  } else if (name == "film") {
    q.rr(p, 3, 5, 18, 14, 2);
    q.mv(p, 7.5f, 5); q.ln(p, 7.5f, 19);
    q.mv(p, 16.5f, 5); q.ln(p, 16.5f, 19);
    q.mv(p, 3, 12); q.ln(p, 21, 12);
    q.draw(p);
  } else if (name == "tag") {
    q.mv(p, 11, 3.5f); q.ln(p, 5.5f, 3.5f);
    q.arc(p, 3.5f, 5.5f, 2, false, false);
    q.ln(p, 3.5f, 11); q.ln(p, 13, 20.5f); q.ln(p, 21, 12.5f); q.end(p);
    q.ell(p, 7.8f, 7.8f, 1.3f, 1.3f);
    q.draw(p);
  } else if (name == "ban") {
    q.ell(p, 12, 12, 8.6f, 8.6f);
    q.mv(p, 6.2f, 17.8f); q.ln(p, 17.8f, 6.2f);
    q.draw(p);
  } else if (name == "filter") {
    q.mv(p, 4, 7); q.ln(p, 14, 7);
    q.mv(p, 18, 7); q.ln(p, 20, 7);
    q.mv(p, 4, 17); q.ln(p, 8, 17);
    q.mv(p, 12, 17); q.ln(p, 20, 17);
    q.ell(p, 16, 7, 2, 2);
    q.ell(p, 10, 17, 2, 2);
    q.draw(p);
  } else if (name == "verify") {
    GraphicsPath st;
    for (int i = 0; i < 12; i++) {
      float a = (-90.0f + (float)i * 30.0f) * kPi / 180.0f;
      float r = (i % 2 == 0) ? 8.6f : 7.4f;
      float px = 12 + r * cosf(a), py = 12 + r * sinf(a);
      if (i == 0) q.mv(st, px, py);
      else q.ln(st, px, py);
    }
    q.end(st);
    q.draw(st);
    q.mv(p, 9.2f, 12); q.ln(p, 11.2f, 14); q.ln(p, 15.2f, 10);
    q.draw(p);
  } else if (name == "logout_all") {
    q.mv(p, 9, 4.5f); q.ln(p, 5.5f, 4.5f);
    q.arc(p, 3.5f, 6.5f, 2, false, false);
    q.ln(p, 3.5f, 17.5f);
    q.arc(p, 5.5f, 19.5f, 2, false, false);
    q.ln(p, 9, 19.5f);
    q.mv(p, 15, 8.5f); q.ln(p, 18.5f, 12); q.ln(p, 15, 15.5f);
    q.mv(p, 8, 12); q.ln(p, 18, 12);
    q.draw(p);
  } else if (name == "download") {
    q.mv(p, 12, 4); q.ln(p, 12, 15);
    q.mv(p, 7.5f, 11); q.ln(p, 12, 15.5f); q.ln(p, 16.5f, 11);
    q.mv(p, 4.5f, 20); q.ln(p, 19.5f, 20);
    q.draw(p);
  } else if (name == "upload") {
    q.mv(p, 12, 20); q.ln(p, 12, 9);
    q.mv(p, 7.5f, 13); q.ln(p, 12, 8.5f); q.ln(p, 16.5f, 13);
    q.mv(p, 4.5f, 4); q.ln(p, 19.5f, 4);
    q.draw(p);
  } else if (name == "grid") {
    q.rr(p, 3.5f, 3.5f, 7, 7, 1.4f);
    q.rr(p, 13.5f, 3.5f, 7, 7, 1.4f);
    q.rr(p, 3.5f, 13.5f, 7, 7, 1.4f);
    q.rr(p, 13.5f, 13.5f, 7, 7, 1.4f);
    q.draw(p);
  } else if (name == "megaphone") {
    q.mv(p, 4, 10.5f); q.ln(p, 4, 13.5f); q.ln(p, 7, 14.3f); q.ln(p, 7, 9.7f); q.end(p);
    q.mv(p, 7, 9.7f); q.ln(p, 19, 5); q.ln(p, 19, 19); q.ln(p, 7, 14.3f);
    q.mv(p, 9.5f, 15); q.ln(p, 9.5f, 19.5f); q.ln(p, 12.5f, 19.5f); q.ln(p, 12.5f, 16);
    q.draw(p);
  } else {
    return false;
  }
  return true;
}

const char* const kNames[] = {
    "home", "chat", "users", "stories", "settings", "search", "send", "attach", "image", "video",
    "music", "file", "heart", "comment", "link", "trash", "edit", "reply", "react", "plus",
    "close", "arrow-left", "arrow-right", "arrow-up", "chevron-left", "chevron-right",
    "chevron-down", "pin", "mute", "eye", "lock", "mail", "phone", "key", "qr", "shield",
    "copy", "check", "refresh", "logout", "user", "bell", "chart", "globe", "dots", "crown",
    "star", "info", "warning", "clock", "camera", "film", "tag", "ban", "filter", "verify",
    "logout_all", "download", "upload", "grid", "megaphone"};

} // namespace

bool hasIcon(const std::string& name) {
  for (const char* n : kNames) {
    if (name == n) return true;
  }
  return false;
}

void drawIcon(Graphics& g, const std::string& name, int x, int y, int size, Color color) {
  if (size <= 0) return;
  if (!drawNamed(g, name, x, y, size, color)) {
    drawNamed(g, "info", x, y, size, color);
  }
}

} // namespace ui
} // namespace om
