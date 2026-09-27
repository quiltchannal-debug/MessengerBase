// OrangeM Desktop - полная реализация интерфейса главного окна.
// Win32 + GDI/GDI+ (owner-draw, двойная буферизация). Все размеры - из theme.h,
// сетевых вызовов здесь нет: только колбэки UiCallbacks и сообщения приложения.
#include "util.h"
#include "ui.h"
#include "theme.h"
#include "icons.h"

#include <objidl.h>
#include <gdiplus.h>
#include <commctrl.h>

#include <algorithm>
#include <cmath>
#include <ctime>
#include <map>
#include <string>
#include <vector>

#pragma comment(lib, "comctl32.lib")

namespace om {
namespace theme {

static HFONT g_ui = nullptr, g_bold = nullptr, g_small = nullptr, g_mono = nullptr,
             g_title = nullptr;

void initFonts() {
  if (!g_ui) {
    g_ui = CreateFontW(-15, 0, 0, 0, FW_NORMAL, 0, 0, 0, DEFAULT_CHARSET, 0, 0, CLEARTYPE_QUALITY,
                       DEFAULT_PITCH, L"Segoe UI");
    g_bold = CreateFontW(-15, 0, 0, 0, FW_SEMIBOLD, 0, 0, 0, DEFAULT_CHARSET, 0, 0,
                         CLEARTYPE_QUALITY, DEFAULT_PITCH, L"Segoe UI");
    g_small = CreateFontW(-12, 0, 0, 0, FW_NORMAL, 0, 0, 0, DEFAULT_CHARSET, 0, 0,
                          CLEARTYPE_QUALITY, DEFAULT_PITCH, L"Segoe UI");
    g_mono = CreateFontW(-13, 0, 0, 0, FW_NORMAL, 0, 0, 0, DEFAULT_CHARSET, 0, 0,
                         CLEARTYPE_QUALITY, DEFAULT_PITCH, L"Consolas");
    g_title = CreateFontW(-19, 0, 0, 0, FW_SEMIBOLD, 0, 0, 0, DEFAULT_CHARSET, 0, 0,
                          CLEARTYPE_QUALITY, DEFAULT_PITCH, L"Segoe UI");
  }
}
HFONT fontUI() { return g_ui; }
HFONT fontBold() { return g_bold; }
HFONT fontSmall() { return g_small; }
HFONT fontMono() { return g_mono; }
HFONT fontTitle() { return g_title; }

} // namespace theme
} // namespace om

namespace om {
namespace ui {

// ------------------------------------------------------------------ идентификаторы
#define IDC_SEARCH 2001
#define IDC_MSG 2002
#define IDC_COMPOSER 2003
#define IDC_SERVER 2004
#define IDC_COMMENT 2005
#define IDC_COMMENT_OK 2006
#define IDC_COMMENT_CANCEL 2007
#define IDT_TOAST 3001
#define IDR_LOGO 102

using namespace Gdiplus;

// ---- целочисленные min/max (RECT-поля имеют тип LONG) ----
static int imin(int a, int b) { return a < b ? a : b; }
static int imax(int a, int b) { return a > b ? a : b; }

// ------------------------------------------------------------------ состояние
static UiState g_state;
static UiCallbacks g_cb;
static std::function<bool(HWND, UINT, WPARAM, LPARAM, LRESULT&)> g_app;
static HWND g_hwnd = nullptr;
static HINSTANCE g_inst = nullptr;

static HWND g_searchEdit = nullptr;
static HWND g_msgEdit = nullptr;
static HWND g_composerEdit = nullptr;
static HWND g_serverEdit = nullptr;

static HWND g_commentWnd = nullptr;
static HWND g_commentEdit = nullptr;
static long long g_commentPostId = 0;

static HDC g_dc = nullptr;       // измерительный DC
static HBRUSH g_fillBrush = nullptr;
static COLORREF g_fillColor = 0xFFFFFFFF;
static Image* g_logo = nullptr;

static std::wstring g_toast;
static bool g_toastErr = false;

static std::map<HWND, WNDPROC> g_oldProcs;
static bool g_tracking = false;

static int g_scrollList = 0;
static int g_scrollMsgs = 0;
static int g_scrollFeed = 0;
static int g_scrollProfile = 0;
static int g_maxList = 0, g_maxMsgs = 0, g_maxFeed = 0, g_maxProfile = 0;
static bool g_msgsToBottom = true;

// ------------------------------------------------------------------ геометрия / попадания
enum HitKind {
  HK_NONE = 0,
  HK_SECTION,
  HK_CHAT,
  HK_COMMUNITY_CARD,
  HK_COMMUNITY_JOIN,
  HK_COMMUNITY_CHAT,
  HK_POST_LIKE,
  HK_POST_COMMENT,
  HK_POST_LINK,
  HK_STORY,
  HK_ACTION
};

enum ActionId {
  ACT_NONE = 0,
  ACT_NEW_CHAT,
  ACT_REFRESH,
  ACT_LOGOUT,
  ACT_ATTACH,
  ACT_SEND,
  ACT_PUBLISH,
  ACT_SEARCH_CLEAR,
  ACT_STORY_ADD,
  ACT_POST_ADD,
  ACT_SERVER_SAVE,
  ACT_CHAT_SEARCH
};

struct Hit {
  RECT rc;
  HitKind kind;
  long long id;
  int act;
};

static std::vector<Hit> g_hits;
static long long g_hover = 0;

static long long hitKey(HitKind k, long long id, int act) {
  return (long long)k * 1000003LL + id * 17LL + (long long)act;
}

static void addHit(HitKind k, const RECT& r, long long id = 0, int act = 0) {
  Hit h;
  h.rc = r;
  h.kind = k;
  h.id = id;
  h.act = act;
  g_hits.push_back(h);
}

static const Hit* hitAt(POINT p) {
  for (size_t i = g_hits.size(); i-- > 0;) {
    if (PtInRect(&g_hits[i].rc, p)) return &g_hits[i];
  }
  return nullptr;
}

struct Layout {
  int w = 0, h = 0;
  RECT sidebar{}, list{}, main{}, status{};
  bool hasList = false;
  RECT searchBox{};
  RECT msgEdit{}, sendBtn{}, attachBtn{};
  RECT composerEdit{}, publishBtn{};
  RECT serverEdit{};
  bool showSearch = false, showMsg = false, showComposer = false, showServer = false;
};

static bool sectionHasList(int s) {
  return s == SEC_CHATS || s == SEC_COMMUNITIES || s == SEC_STORIES;
}

static Layout computeLayout() {
  Layout L;
  RECT rc;
  GetClientRect(g_hwnd, &rc);
  L.w = rc.right;
  L.h = rc.bottom;
  int sh = theme::statusH;
  L.status.left = 0; L.status.top = L.h - sh; L.status.right = L.w; L.status.bottom = L.h;
  L.sidebar.left = 0; L.sidebar.top = 0; L.sidebar.right = theme::sidebarW;
  L.sidebar.bottom = L.h - sh;
  int x = theme::sidebarW;
  L.hasList = sectionHasList(g_state.section);
  if (L.hasList) {
    L.list.left = x; L.list.top = 0; L.list.right = x + theme::listW; L.list.bottom = L.h - sh;
    x += theme::listW;
  }
  L.main.left = x; L.main.top = 0; L.main.right = L.w; L.main.bottom = L.h - sh;

  int sec = g_state.section;
  if (sec == SEC_CHATS || sec == SEC_COMMUNITIES) {
    L.showSearch = true;
    L.searchBox.left = L.list.left + theme::pad;
    L.searchBox.top = theme::headerH + 8;
    L.searchBox.right = L.list.right - theme::pad;
    L.searchBox.bottom = L.searchBox.top + 32;
  } else if (sec == SEC_FEED) {
    L.showSearch = true;
    L.searchBox.left = L.main.left + theme::pad;
    L.searchBox.top = theme::headerH + 8;
    L.searchBox.right = L.main.left + theme::pad + 320;
    L.searchBox.bottom = L.searchBox.top + 32;
  }

  if (sec == SEC_CHATS && g_state.activeChat != 0) {
    L.showMsg = true;
    int y = L.main.bottom - 60;
    L.attachBtn.left = L.main.left + theme::pad;
    L.attachBtn.top = y + 13;
    L.attachBtn.right = L.attachBtn.left + 34;
    L.attachBtn.bottom = L.attachBtn.top + 34;
    L.sendBtn.right = L.main.right - theme::pad;
    L.sendBtn.top = y + 13;
    L.sendBtn.left = L.sendBtn.right - 34;
    L.sendBtn.bottom = L.sendBtn.top + 34;
    L.msgEdit.left = L.attachBtn.right + 10;
    L.msgEdit.top = y + 15;
    L.msgEdit.right = L.sendBtn.left - 10;
    L.msgEdit.bottom = L.msgEdit.top + 30;
  }
  if (sec == SEC_FEED) {
    L.showComposer = true;
    RECT c;
    c.left = L.main.left + theme::pad;
    c.right = L.main.right - theme::pad;
    c.top = L.searchBox.bottom + 12;
    c.bottom = c.top + 118;
    L.composerEdit.left = c.left + 12;
    L.composerEdit.top = c.top + 12;
    L.composerEdit.right = c.right - 12;
    L.composerEdit.bottom = c.bottom - 52;
    L.publishBtn.right = c.right - 12;
    L.publishBtn.bottom = c.bottom - 12;
    L.publishBtn.left = L.publishBtn.right - 150;
    L.publishBtn.top = L.publishBtn.bottom - 32;
  }
  if (sec == SEC_PROFILE) {
    L.showServer = true;
    int cardW = imin(760, L.main.right - L.main.left - theme::pad * 2);
    int cx = L.main.left + theme::pad;
    int cy = theme::headerH + 16 - g_scrollProfile;
    L.serverEdit.left = cx + 24;
    L.serverEdit.top = cy + 268;
    L.serverEdit.right = cx + cardW - 24;
    L.serverEdit.bottom = L.serverEdit.top + 32;
  }
  return L;
}

// ------------------------------------------------------------------ утилиты рисования
static Color gcol(COLORREF c) {
  return Color(255, GetRValue(c), GetGValue(c), GetBValue(c));
}
static COLORREF blend(COLORREF a, COLORREF b, float t) {
  if (t < 0) t = 0;
  if (t > 1) t = 1;
  int r = (int)(GetRValue(a) * (1 - t) + GetRValue(b) * t + 0.5f);
  int g = (int)(GetGValue(a) * (1 - t) + GetGValue(b) * t + 0.5f);
  int bl = (int)(GetBValue(a) * (1 - t) + GetBValue(b) * t + 0.5f);
  return RGB(r, g, bl);
}

static void gdiFill(HDC dc, const RECT& r, COLORREF c) {
  if (!g_fillBrush || g_fillColor != c) {
    if (g_fillBrush) DeleteObject(g_fillBrush);
    g_fillBrush = CreateSolidBrush(c);
    g_fillColor = c;
  }
  FillRect(dc, &r, g_fillBrush);
}

static void gdiHLine(HDC dc, int x1, int x2, int y, COLORREF c) {
  RECT r = {x1, y, x2, y + 1};
  gdiFill(dc, r, c);
}
static void gdiVLine(HDC dc, int x, int y1, int y2, COLORREF c) {
  RECT r = {x, y1, x + 1, y2};
  gdiFill(dc, r, c);
}

static void roundPath(GraphicsPath& p, const RECT& r, int rad) {
  float x = (float)r.left, y = (float)r.top;
  float w = (float)(r.right - r.left), h = (float)(r.bottom - r.top);
  if (w <= 0 || h <= 0) return;
  float d = (float)(rad * 2);
  if (d > w) d = w;
  if (d > h) d = h;
  if (d <= 1.0f) {
    p.AddRectangle(RectF(x, y, w, h));
    return;
  }
  p.AddArc(x, y, d, d, 180.0f, 90.0f);
  p.AddArc(x + w - d, y, d, d, 270.0f, 90.0f);
  p.AddArc(x + w - d, y + h - d, d, d, 0.0f, 90.0f);
  p.AddArc(x, y + h - d, d, d, 90.0f, 90.0f);
  p.CloseFigure();
}

static void roundFill(Graphics& g, const RECT& r, int rad, COLORREF c) {
  GraphicsPath p;
  roundPath(p, r, rad);
  SolidBrush b(gcol(c));
  g.FillPath(&b, &p);
}

static void roundBox(Graphics& g, const RECT& r, int rad, COLORREF fill, COLORREF border) {
  GraphicsPath p;
  roundPath(p, r, rad);
  SolidBrush b(gcol(fill));
  g.FillPath(&b, &p);
  if (border != fill) {
    Pen pen(gcol(border), 1.0f);
    g.DrawPath(&pen, &p);
  }
}

static void circleFill(Graphics& g, int cx, int cy, int rad, COLORREF c) {
  SolidBrush b(gcol(c));
  g.FillEllipse(&b, cx - rad, cy - rad, rad * 2, rad * 2);
}
static void circleStroke(Graphics& g, int cx, int cy, int rad, COLORREF c, float wid) {
  Pen pen(gcol(c), wid);
  g.DrawEllipse(&pen, cx - rad, cy - rad, rad * 2, rad * 2);
}

static void textIn(Graphics& g, const std::wstring& s, RECT r, COLORREF c, HFONT f, UINT flags) {
  if (s.empty()) return;
  HDC dc = g.GetHDC();
  SetBkMode(dc, TRANSPARENT);
  SetTextColor(dc, c);
  HFONT old = (HFONT)SelectObject(dc, f);
  DrawTextW(dc, s.c_str(), (int)s.size(), &r, flags | DT_NOPREFIX);
  SelectObject(dc, old);
  g.ReleaseHDC(dc);
}

static void measureText(const std::wstring& s, HFONT f, int maxW, RECT& out, UINT extra = 0) {
  out.left = 0;
  out.top = 0;
  out.right = maxW;
  out.bottom = 0;
  if (!g_dc) return;
  HFONT old = (HFONT)SelectObject(g_dc, f);
  DrawTextW(g_dc, s.c_str(), (int)s.size(), &out,
            DT_CALCRECT | DT_WORDBREAK | DT_NOPREFIX | extra);
  SelectObject(g_dc, old);
}

static int textWidth(const std::wstring& s, HFONT f) {
  if (s.empty() || !g_dc) return 0;
  HFONT old = (HFONT)SelectObject(g_dc, f);
  SIZE sz;
  GetTextExtentPoint32W(g_dc, s.c_str(), (int)s.size(), &sz);
  SelectObject(g_dc, old);
  return sz.cx;
}

static void iconAt(Graphics& g, const char* name, const RECT& r, COLORREF c) {
  int size = imin(r.right - r.left, r.bottom - r.top);
  int x = r.left + ((r.right - r.left) - size) / 2;
  int y = r.top + ((r.bottom - r.top) - size) / 2;
  drawIcon(g, name, x, y, size, gcol(c));
}

static void iconButton(Graphics& g, const RECT& r, const char* name, bool hot, bool primary) {
  COLORREF fill = primary ? theme::orange : (hot ? theme::panel2 : theme::panel);
  COLORREF brd = primary ? theme::orange : theme::line;
  roundBox(g, r, 6, fill, brd);
  RECT ic = r;
  ic.left += 8; ic.right -= 8; ic.top += 8; ic.bottom -= 8;
  iconAt(g, name, ic, primary ? theme::onOrange : theme::text);
}

// ------------------------------------------------------------------ текст-помощники
static std::wstring initialsOf(const std::string& name) {
  std::wstring w = u2w(name);
  std::wstring out;
  bool start = true;
  for (wchar_t c : w) {
    if (c == L' ' || c == L'_' || c == L'-' || c == L'.' || c == L'@') {
      start = true;
      continue;
    }
    if (!start) continue;
    start = false;
    if (c >= L'a' && c <= L'z') c = (wchar_t)(c - 32);
    else if (c >= L'а' && c <= L'я') c = (wchar_t)(c - 32);
    else if (c == L'ё') c = L'Ё';
    out += c;
    if (out.size() >= 2) break;
  }
  return out;
}

static std::wstring two(int v) {
  std::wstring s = std::to_wstring(v);
  if (s.size() < 2) s = L"0" + s;
  return s;
}

static std::wstring shortTime(long long sec) {
  if (sec <= 0) return L"";
  time_t t = (time_t)sec;
  struct tm* lt = localtime(&t);
  if (!lt) return L"";
  return two(lt->tm_hour) + L":" + two(lt->tm_min);
}

static std::wstring dateOrTime(long long sec) {
  if (sec <= 0) return L"";
  time_t t = (time_t)sec;
  struct tm* lt = localtime(&t);
  time_t cur = time(nullptr);
  struct tm* now = localtime(&cur);
  if (!lt || !now) return L"";
  if (lt->tm_year == now->tm_year && lt->tm_yday == now->tm_yday)
    return two(lt->tm_hour) + L":" + two(lt->tm_min);
  if (lt->tm_year == now->tm_year)
    return two(lt->tm_mday) + L"." + two(lt->tm_mon + 1);
  return two(lt->tm_mday) + L"." + two(lt->tm_mon + 1) + L"." +
         two((lt->tm_year + 1900) % 100);
}

static std::wstring countText(long long n) {
  return std::to_wstring(n);
}

static std::wstring getEditTextW(HWND h) {
  if (!h) return L"";
  int n = GetWindowTextLengthW(h);
  std::wstring s((size_t)n + 1, L'\0');
  GetWindowTextW(h, &s[0], n + 1);
  s.resize((size_t)n);
  return s;
}

static bool looksLikeError(const std::string& s) {
  return s.find("шибк") != std::string::npos || s.find("Не ") != std::string::npos ||
         s.find("не удалось") != std::string::npos || s.find("недоступ") != std::string::npos ||
         s.find("error") != std::string::npos || s.find("fail") != std::string::npos ||
         s.find("FAIL") != std::string::npos;
}

// ------------------------------------------------------------------ логотип из ресурса
static void loadLogo() {
  if (g_logo || !g_inst) return;
  HRSRC res = FindResourceW(g_inst, MAKEINTRESOURCEW(IDR_LOGO), RT_RCDATA);
  if (!res) return;
  DWORD sz = SizeofResource(g_inst, res);
  HGLOBAL hres = LoadResource(g_inst, res);
  if (!hres || !sz) return;
  void* src = LockResource(hres);
  if (!src) return;
  HGLOBAL mem = GlobalAlloc(GMEM_MOVEABLE, sz);
  if (!mem) return;
  void* dst = GlobalLock(mem);
  if (!dst) {
    GlobalFree(mem);
    return;
  }
  memcpy(dst, src, sz);
  GlobalUnlock(mem);
  IStream* st = nullptr;
  if (CreateStreamOnHGlobal(mem, TRUE, &st) == S_OK && st) {
    Image* img = Image::FromStream(st);
    st->Release();
    if (img && img->GetLastStatus() == Ok) g_logo = img;
    else delete img;
  } else {
    GlobalFree(mem);
  }
}

static void drawLogo(Graphics& g, const RECT& r) {
  if (g_logo) {
    GraphicsPath clip;
    roundPath(clip, r, 6);
    g.SetClip(&clip);
    g.DrawImage(g_logo, Rect(r.left, r.top, r.right - r.left, r.bottom - r.top));
    g.ResetClip();
    return;
  }
  roundFill(g, r, 6, theme::orange);
  RECT t = r;
  textIn(g, L"OM", t, theme::onOrange, theme::fontBold(),
         DT_CENTER | DT_VCENTER | DT_SINGLELINE);
}

// ------------------------------------------------------------------ навигация
static const char* sectionTitle(int s) {
  switch (s) {
    case SEC_FEED: return "Лента";
    case SEC_COMMUNITIES: return "Сообщества";
    case SEC_STORIES: return "Истории";
    case SEC_PROFILE: return "Профиль";
    default: return "Чаты";
  }
}

// ------------------------------------------------------------------ отрисовка: сайдбар
static void paintSidebar(Graphics& g, HDC dc, const Layout& L) {
  gdiFill(dc, L.sidebar, theme::bg2);
  gdiVLine(dc, L.sidebar.right - 1, 0, L.sidebar.bottom, theme::line);

  RECT logo = {theme::pad, theme::pad + 2, theme::pad + 32, theme::pad + 34};
  drawLogo(g, logo);

  RECT tr = {logo.right + 12, logo.top, L.sidebar.right - theme::pad, logo.bottom};
  textIn(g, L"OrangeM", tr, theme::orange, theme::fontTitle(), DT_LEFT | DT_VCENTER | DT_SINGLELINE);

  struct Item { int sec; const char* icon; const wchar_t* label; };
  Item items[5] = {
      {SEC_FEED, "home", L"Лента"},
      {SEC_CHATS, "chat", L"Чаты"},
      {SEC_COMMUNITIES, "users", L"Сообщества"},
      {SEC_STORIES, "stories", L"Истории"},
      {SEC_PROFILE, "settings", L"Настройки"}};

  int y = 78;
  for (int i = 0; i < 5; i++) {
    RECT row = {0, y, L.sidebar.right, y + 40};
    bool active = (g_state.section == items[i].sec);
    bool hot = (g_hover == hitKey(HK_SECTION, items[i].sec, 0));
    if (active) gdiFill(dc, row, theme::panel);
    else if (hot) gdiFill(dc, row, theme::panel2);
    if (active) gdiFill(dc, RECT{0, y, 2, y + 40}, theme::orange);
    RECT ic = {20, y + 11, 38, y + 29};
    iconAt(g, items[i].icon, ic, active ? theme::orange : theme::muted);
    RECT tx = {46, y, L.sidebar.right - 12, y + 40};
    textIn(g, items[i].label, tx, active ? theme::text : theme::muted, theme::fontUI(),
           DT_LEFT | DT_VCENTER | DT_SINGLELINE);
    addHit(HK_SECTION, row, items[i].sec, 0);
    y += 40;
  }

  // карточка пользователя внизу
  int bottom = L.sidebar.bottom;
  RECT card = {12, bottom - 100, L.sidebar.right - 12, bottom - 40};
  roundBox(g, card, 8, theme::panel, theme::line);
  std::string nm = g_state.me.displayName.empty() ? g_state.me.username : g_state.me.displayName;
  if (nm.empty()) nm = "Гость";
  circleFill(g, card.left + 32, card.top + 30, 16, theme::panel2);
  circleStroke(g, card.left + 32, card.top + 30, 16, theme::line2, 1.0f);
  RECT ini = {card.left + 16, card.top + 14, card.left + 48, card.top + 46};
  textIn(g, initialsOf(nm), ini, theme::text, theme::fontBold(),
         DT_CENTER | DT_VCENTER | DT_SINGLELINE);
  RECT nr = {card.left + 58, card.top + 14, card.right - 8, card.top + 34};
  textIn(g, u2w(nm), nr, theme::text, theme::fontBold(),
         DT_LEFT | DT_VCENTER | DT_SINGLELINE | DT_END_ELLIPSIS);
  std::string oid = g_state.me.orangeId.empty() ? "OM-—" : g_state.me.orangeId;
  RECT orr = {card.left + 58, card.top + 34, card.right - 8, card.top + 52};
  textIn(g, u2w(oid), orr, theme::muted, theme::fontMono(),
         DT_LEFT | DT_VCENTER | DT_SINGLELINE | DT_END_ELLIPSIS);

  // состояние соединения
  int cy = bottom - 26;
  COLORREF cc = g_state.realtime ? theme::ok : theme::danger;
  circleFill(g, 20, cy + 8, 4, cc);
  RECT cr = {30, cy, L.sidebar.right - theme::pad, cy + 18};
  textIn(g, g_state.realtime ? L"realtime подключён" : L"нет связи", cr, theme::muted,
         theme::fontSmall(), DT_LEFT | DT_VCENTER | DT_SINGLELINE);
}

// ------------------------------------------------------------------ отрисовка: строка состояния
static void paintStatusBar(Graphics& g, HDC dc, const Layout& L) {
  gdiFill(dc, L.status, theme::bg2);
  gdiHLine(dc, 0, L.w, L.status.top, theme::line);
  std::wstring st = u2w(g_state.status);
  if (st.empty()) st = L"Готово";
  RECT tr = {theme::pad, L.status.top, L.w - 260, L.status.bottom};
  textIn(g, st, tr, theme::muted, theme::fontSmall(),
         DT_LEFT | DT_VCENTER | DT_SINGLELINE | DT_END_ELLIPSIS);
  std::wstring rt = g_state.realtime ? L"realtime подключён" : L"нет связи";
  COLORREF cc = g_state.realtime ? theme::ok : theme::danger;
  int w = textWidth(rt, theme::fontSmall());
  int x = L.w - theme::pad - w;
  RECT rr = {x, L.status.top, L.w - theme::pad, L.status.bottom};
  textIn(g, rt, rr, theme::muted, theme::fontSmall(),
         DT_LEFT | DT_VCENTER | DT_SINGLELINE);
  circleFill(g, x - 14, L.status.top + theme::statusH / 2, 4, cc);
}

// ------------------------------------------------------------------ отрисовка: список
static void paintListPanel(Graphics& g, HDC dc, const Layout& L) {
  gdiFill(dc, L.list, theme::panel);
  gdiVLine(dc, L.list.right - 1, 0, L.list.bottom, theme::line);

  RECT hr = {L.list.left + theme::pad, 0, L.list.right - theme::pad, theme::headerH};
  textIn(g, u2w(sectionTitle(g_state.section)), hr, theme::text, theme::fontTitle(),
         DT_LEFT | DT_VCENTER | DT_SINGLELINE);

  if (g_state.section == SEC_CHATS) {
    // кнопка «Новый чат» в шапке
    RECT add = {L.list.right - theme::pad - 34, (theme::headerH - 34) / 2,
                L.list.right - theme::pad, (theme::headerH - 34) / 2 + 34};
    bool hot = (g_hover == hitKey(HK_ACTION, 0, ACT_NEW_CHAT));
    iconButton(g, add, "plus", hot, false);
    addHit(HK_ACTION, add, 0, ACT_NEW_CHAT);
  }

  if (L.showSearch) {
    gdiFill(dc, L.searchBox, theme::bg2);
    roundBox(g, L.searchBox, 6, theme::bg2, theme::line);
    RECT ic = {L.searchBox.left + 8, L.searchBox.top + 7, L.searchBox.left + 26,
               L.searchBox.top + 25};
    iconAt(g, "search", ic, theme::muted);
    bool hasText = g_searchEdit && GetWindowTextLengthW(g_searchEdit) > 0;
    if (hasText) {
      RECT cl = {L.searchBox.right - 28, L.searchBox.top + 4, L.searchBox.right - 4,
                 L.searchBox.bottom - 4};
      bool hot = (g_hover == hitKey(HK_ACTION, 0, ACT_SEARCH_CLEAR));
      if (hot) roundFill(g, cl, 4, theme::panel2);
      iconAt(g, "close", cl, theme::muted);
      addHit(HK_ACTION, cl, 0, ACT_SEARCH_CLEAR);
    }
  }

  int top = L.hasList && L.showSearch ? L.searchBox.bottom + 8 : theme::headerH;

  // содержимое списка
  int total = 0, rowH = 68;
  if (g_state.section == SEC_CHATS) total = (int)g_state.chats.size();
  else if (g_state.section == SEC_COMMUNITIES) total = (int)g_state.communities.size();
  else if (g_state.section == SEC_STORIES) total = (int)g_state.stories.size();

  int viewH = L.list.bottom - top;
  int contentH = total * rowH;
  g_maxList = imax(0, contentH - viewH);
  if (g_scrollList > g_maxList) g_scrollList = g_maxList;
  if (g_scrollList < 0) g_scrollList = 0;

  g.SetClip(RectF((REAL)L.list.left, (REAL)top, (REAL)(L.list.right - L.list.left),
                  (REAL)viewH));
  int y = top - g_scrollList;
  for (int i = 0; i < total; i++, y += rowH) {
    if (y + rowH < top || y > L.list.bottom) continue;
    RECT row = {L.list.left, y, L.list.right - 1, y + rowH};
    long long id = 0;
    std::wstring title, sub;
    bool active = false, unseen = false;
    long long badge = 0;
    bool pinned = false, muted = false;
    const char* subIcon = nullptr;

    if (g_state.section == SEC_CHATS) {
      const ChatItem& c = g_state.chats[i];
      id = c.id;
      title = u2w(c.title.empty() ? c.peerUsername : c.title);
      sub = u2w(c.preview);
      active = (g_state.activeChat == c.id);
      badge = c.unread;
      pinned = c.pinned;
      muted = c.muted;
      subIcon = nullptr;
    } else if (g_state.section == SEC_COMMUNITIES) {
      const Community& c = g_state.communities[i];
      id = c.id;
      title = u2w(c.name);
      sub = u2w(c.description);
      badge = 0;
    } else {
      const StoryGroup& s = g_state.stories[i];
      id = s.id;
      title = u2w(s.name);
      sub = u2w(s.caption);
      unseen = s.hasUnseen;
      badge = s.count;
    }

    HitKind kind = g_state.section == SEC_CHATS
                       ? HK_CHAT
                       : (g_state.section == SEC_COMMUNITIES ? HK_COMMUNITY_CARD : HK_STORY);
    bool hot = (g_hover == hitKey(kind, id, 0));
    if (active) gdiFill(dc, row, theme::panel2);
    else if (hot) gdiFill(dc, row, theme::bg2);
    if (active) gdiFill(dc, RECT{row.left, row.top, row.left + 2, row.bottom}, theme::orange);
    gdiHLine(dc, row.left + 12, row.right - 12, row.bottom - 1, theme::line);

    int av = 40;
    int ax = row.left + 12, ay = row.top + (rowH - av) / 2;
    if (unseen) circleStroke(g, ax + av / 2, ay + av / 2, av / 2 + 3, theme::orange, 2.0f);
    else if (g_state.section == SEC_STORIES)
      circleStroke(g, ax + av / 2, ay + av / 2, av / 2 + 3, theme::line2, 2.0f);
    circleFill(g, ax + av / 2, ay + av / 2, av / 2, theme::panel2);
    RECT ai = {ax, ay, ax + av, ay + av};
    textIn(g, initialsOf(w2u(title)), ai, theme::text, theme::fontBold(),
           DT_CENTER | DT_VCENTER | DT_SINGLELINE);

    int rightPad = row.right - 12;
    if (badge > 0) {
      std::wstring bt = countText(badge);
      int bw = imax(20, textWidth(bt, theme::fontSmall()) + 12);
      RECT bb = {rightPad - bw, row.top + rowH / 2 - 10, rightPad, row.top + rowH / 2 + 10};
      roundFill(g, bb, 10, theme::orange);
      textIn(g, bt, bb, theme::onOrange, theme::fontSmall(),
             DT_CENTER | DT_VCENTER | DT_SINGLELINE);
      rightPad = bb.left - 8;
    }

    RECT tr = {ax + av + 12, row.top + 12, rightPad, row.top + 32};
    textIn(g, title, tr, active ? theme::text : (hot ? theme::text : theme::text),
           theme::fontBold(), DT_LEFT | DT_VCENTER | DT_SINGLELINE | DT_END_ELLIPSIS);

    RECT sr = {ax + av + 12, row.top + 32, row.right - 12, row.top + 54};
    textIn(g, sub, sr, theme::muted, theme::fontSmall(),
           DT_LEFT | DT_VCENTER | DT_SINGLELINE | DT_END_ELLIPSIS);

    if (g_state.section == SEC_CHATS) {
      const ChatItem& c = g_state.chats[i];
      std::wstring tm = dateOrTime(c.lastAt);
      if (!tm.empty()) {
        int tw = textWidth(tm, theme::fontSmall());
        RECT rr = {rightPad - tw, row.top + 12, rightPad, row.top + 30};
        textIn(g, tm, rr, theme::muted2, theme::fontSmall(),
               DT_RIGHT | DT_VCENTER | DT_SINGLELINE);
      }
      int px = rightPad - 18;
      if (muted) {
        RECT mr = {px, row.top + 36, px + 16, row.top + 52};
        iconAt(g, "mute", mr, theme::muted2);
        px -= 20;
      }
      if (pinned) {
        RECT pr = {px, row.top + 36, px + 16, row.top + 52};
        iconAt(g, "pin", pr, theme::orange);
      }
    }
    (void)subIcon;
    addHit(kind, row, id, 0);
  }
  g.ResetClip();

  if (total == 0) {
    RECT er = {L.list.left + theme::pad, top + 24, L.list.right - theme::pad, top + 120};
    textIn(g, g_state.loading ? L"Загрузка…" : L"Пусто", er, theme::muted2, theme::fontUI(),
           DT_CENTER | DT_TOP | DT_WORDBREAK);
  }
}

// ------------------------------------------------------------------ отрисовка: чаты
static void paintChatMessages(Graphics& g, HDC dc, const Layout& L) {
  int areaX = L.main.left;
  int areaW = L.main.right - L.main.left;
  int top = theme::headerH;
  int bottom = L.showMsg ? L.main.bottom - 60 : L.main.bottom;

  // шапка
  RECT hr = {areaX + theme::pad, 0, L.main.right - theme::pad, theme::headerH};
  gdiHLine(dc, areaX, L.main.right, theme::headerH - 1, theme::line);
  std::wstring title = u2w(g_state.chattingWith);
  if (title.empty()) {
    for (const auto& c : g_state.chats)
      if (c.id == g_state.activeChat) title = u2w(c.title.empty() ? c.peerUsername : c.title);
  }
  if (title.empty()) title = L"Чат";
  RECT tr = {hr.left, 8, hr.right - 130, 30};
  textIn(g, title, tr, theme::text, theme::fontBold(),
         DT_LEFT | DT_VCENTER | DT_SINGLELINE | DT_END_ELLIPSIS);
  std::wstring st;
  bool online = g_state.peerOnline;
  for (const auto& c : g_state.chats) {
    if (c.id == g_state.activeChat) {
      online = c.peerOnline;
      if (!c.peerOnline && !c.lastAt) st = L"был(а) недавно";
    }
  }
  st = online ? L"в сети" : (st.empty() ? L"был(а) недавно" : st);
  RECT sr = {hr.left, 28, hr.right - 130, 48};
  textIn(g, st, sr, online ? theme::ok : theme::muted, theme::fontSmall(),
         DT_LEFT | DT_VCENTER | DT_SINGLELINE);

  const char* btns[3] = {"search", "attach", "refresh"};
  const int acts[3] = {ACT_CHAT_SEARCH, ACT_ATTACH, ACT_REFRESH};
  for (int i = 0; i < 3; i++) {
    int bx = L.main.right - theme::pad - 34 - i * 42;
    RECT b = {bx, (theme::headerH - 34) / 2, bx + 34, (theme::headerH - 34) / 2 + 34};
    bool hot = (g_hover == hitKey(HK_ACTION, 0, acts[i]));
    iconButton(g, b, btns[i], hot, false);
    addHit(HK_ACTION, b, 0, acts[i]);
  }
  gdiHLine(dc, areaX, L.main.right, theme::headerH - 1, theme::line);

  // нижняя строка ввода
  if (L.showMsg) {
    RECT bar = {areaX, L.main.bottom - 60, L.main.right, L.main.bottom};
    gdiFill(dc, bar, theme::bg);
    gdiHLine(dc, areaX, L.main.right, bar.top, theme::line);
    roundBox(g, L.msgEdit, 6, theme::bg2, theme::line);
    bool hotA = (g_hover == hitKey(HK_ACTION, 0, ACT_ATTACH));
    iconButton(g, L.attachBtn, "attach", hotA, false);
    addHit(HK_ACTION, L.attachBtn, 0, ACT_ATTACH);
    bool hotS = (g_hover == hitKey(HK_ACTION, 0, ACT_SEND));
    iconButton(g, L.sendBtn, "send", hotS, true);
    addHit(HK_ACTION, L.sendBtn, 0, ACT_SEND);
  }

  // лента сообщений
  int viewH = bottom - top;
  if (viewH < 10) return;

  struct Row { RECT rc; int idx; bool sys; };
  std::vector<Row> rows;
  int maxBubble = imin(620, (int)((float)areaW * 0.72f));
  if (maxBubble < 220) maxBubble = 220;
  int y = top + 10;
  for (size_t i = 0; i < g_state.msgs.size(); i++) {
    const Msg& m = g_state.msgs[i];
    if (m.system) {
      RECT mr;
      std::wstring t = u2w(m.body);
      measureText(t, theme::fontSmall(), areaW - 64, mr);
      int h = mr.bottom + 12;
      Row r;
      r.rc = RECT{areaX, y, areaX + areaW, y + h};
      r.idx = (int)i;
      r.sys = true;
      rows.push_back(r);
      y += h + 4;
      continue;
    }
    std::wstring body = u2w(m.body);
    RECT mr;
    measureText(body, theme::fontUI(), maxBubble - 24, mr);
    bool showName = !m.mine && !m.senderName.empty() && g_state.msgs.size() > 1;
    int bw = imin(maxBubble, mr.right + 24);
    if (bw < 90) bw = 90;
    // пересчёт переноса под итоговую ширину
    measureText(body, theme::fontUI(), bw - 24, mr);
    int h = 10 + (showName ? 16 : 0) + mr.bottom + (m.attachment.empty() ? 0 : 24) + 6 + 16 + 10;
    int left = m.mine ? areaX + areaW - theme::pad - bw : areaX + theme::pad;
    Row r;
    r.rc = RECT{left, y, left + bw, y + h};
    r.idx = (int)i;
    r.sys = false;
    rows.push_back(r);
    y += h + 10;
  }
  int contentH = (y - top) + 10;
  g_maxMsgs = imax(0, contentH - viewH);
  if (g_msgsToBottom) {
    g_scrollMsgs = g_maxMsgs;
    g_msgsToBottom = false;
  }
  if (g_scrollMsgs > g_maxMsgs) g_scrollMsgs = g_maxMsgs;
  if (g_scrollMsgs < 0) g_scrollMsgs = 0;

  g.SetClip(RectF((REAL)areaX, (REAL)top, (REAL)areaW, (REAL)viewH));
  for (const Row& r : rows) {
    RECT bb = r.rc;
    bb.top -= g_scrollMsgs;
    bb.bottom -= g_scrollMsgs;
    if (bb.bottom < top || bb.top > bottom) continue;
    const Msg& m = g_state.msgs[r.idx];
    if (r.sys) {
      textIn(g, u2w(m.body), bb, theme::muted2, theme::fontSmall(),
             DT_CENTER | DT_VCENTER | DT_SINGLELINE | DT_END_ELLIPSIS);
      continue;
    }
    COLORREF fill = m.mine ? blend(theme::bg, theme::orange, 0.10f) : theme::panel2;
    COLORREF brd = m.mine ? blend(theme::bg, theme::orange, 0.30f) : theme::line;
    roundBox(g, bb, 8, fill, brd);
    int ty = bb.top + 10;
    if (!m.mine && !m.senderName.empty() && g_state.msgs.size() > 1) {
      RECT nr = {bb.left + 12, ty, bb.right - 12, ty + 16};
      textIn(g, u2w(m.senderName), nr, m.mine ? theme::orange2 : theme::blue, theme::fontSmall(),
             DT_LEFT | DT_TOP | DT_SINGLELINE | DT_END_ELLIPSIS);
      ty += 16;
    }
    RECT br = {bb.left + 12, ty, bb.right - 12, bb.bottom - 26};
    textIn(g, u2w(m.body), br, theme::text, theme::fontUI(), DT_LEFT | DT_TOP | DT_WORDBREAK);
    int ay = br.bottom;
    if (!m.attachment.empty()) {
      RECT ar = {bb.left + 12, ay, bb.right - 12, ay + 22};
      iconAt(g, "image", RECT{ar.left, ar.top + 3, ar.left + 16, ar.top + 19}, theme::muted);
      RECT at = {ar.left + 22, ar.top, ar.right, ar.bottom};
      textIn(g, L"Вложение", at, theme::muted, theme::fontSmall(),
             DT_LEFT | DT_VCENTER | DT_SINGLELINE);
    }
    std::wstring tm = shortTime(m.createdAt);
    RECT mr = {bb.left + 12, bb.bottom - 22, bb.right - 12, bb.bottom - 8};
    textIn(g, tm, mr, theme::muted2, theme::fontSmall(),
           DT_RIGHT | DT_VCENTER | DT_SINGLELINE);
  }
  g.ResetClip();

  if (g_state.msgs.empty()) {
    RECT er = {areaX, top, L.main.right, top + viewH};
    textIn(g, g_state.loading ? L"Загрузка…" : L"Сообщений пока нет", er, theme::muted2,
           theme::fontUI(), DT_CENTER | DT_VCENTER | DT_SINGLELINE);
  }
}

// ------------------------------------------------------------------ отрисовка: лента
static void paintFeed(Graphics& g, HDC dc, const Layout& L) {
  gdiHLine(dc, L.main.left, L.main.right, theme::headerH - 1, theme::line);
  RECT hr = {L.main.left + theme::pad, 0, L.main.left + 200, theme::headerH};
  textIn(g, L"Лента", hr, theme::text, theme::fontTitle(), DT_LEFT | DT_VCENTER | DT_SINGLELINE);
  RECT rb = {L.main.right - theme::pad - 34, (theme::headerH - 34) / 2,
             L.main.right - theme::pad, (theme::headerH - 34) / 2 + 34};
  bool hotR = (g_hover == hitKey(HK_ACTION, 0, ACT_REFRESH));
  iconButton(g, rb, "refresh", hotR, false);
  addHit(HK_ACTION, rb, 0, ACT_REFRESH);

  if (L.showSearch) {
    roundBox(g, L.searchBox, 6, theme::bg2, theme::line);
    RECT ic = {L.searchBox.left + 8, L.searchBox.top + 7, L.searchBox.left + 26,
               L.searchBox.top + 25};
    iconAt(g, "search", ic, theme::muted);
    if (g_searchEdit && GetWindowTextLengthW(g_searchEdit) > 0) {
      RECT cl = {L.searchBox.right - 28, L.searchBox.top + 4, L.searchBox.right - 4,
                 L.searchBox.bottom - 4};
      if (g_hover == hitKey(HK_ACTION, 0, ACT_SEARCH_CLEAR)) roundFill(g, cl, 4, theme::panel2);
      iconAt(g, "close", cl, theme::muted);
      addHit(HK_ACTION, cl, 0, ACT_SEARCH_CLEAR);
    }
  }

  if (L.showComposer) {
    RECT card = {L.composerEdit.left - 12, L.composerEdit.top - 12, L.composerEdit.right + 12,
                 L.publishBtn.bottom + 12};
    roundBox(g, card, 10, theme::panel, theme::line);
    roundBox(g, L.composerEdit, 6, theme::bg2, theme::line);
    bool hot = (g_hover == hitKey(HK_ACTION, 0, ACT_PUBLISH));
    roundBox(g, L.publishBtn, 6, hot ? theme::orange2 : theme::orange, theme::orange);
    RECT pt = L.publishBtn;
    textIn(g, L"Опубликовать", pt, theme::onOrange, theme::fontBold(),
           DT_CENTER | DT_VCENTER | DT_SINGLELINE);
    addHit(HK_ACTION, L.publishBtn, 0, ACT_PUBLISH);
  }

  int top = L.showComposer ? L.publishBtn.bottom + 24 : theme::headerH + 8;
  int viewH = L.main.bottom - top;
  int cardX = L.main.left + theme::pad;
  int cardW = imin(760, L.main.right - L.main.left - theme::pad * 2);

  g.SetClip(RectF((REAL)L.main.left, (REAL)top, (REAL)(L.main.right - L.main.left),
                  (REAL)viewH));
  int y = top - g_scrollFeed;
  int totalH = 0;
  for (size_t i = 0; i < g_state.posts.size(); i++) {
    const Post& p = g_state.posts[i];
    std::wstring title = u2w(p.title);
    std::wstring body = u2w(p.body);
    RECT tr, br;
    measureText(title, theme::fontBold(), cardW - 48, tr);
    measureText(body, theme::fontUI(), cardW - 48, br);
    int h = 12 + 20 + (title.empty() ? 0 : tr.bottom + 6) + br.bottom + 8;
    if (!p.tags.empty()) h += 28;
    h += 40 + 12;
    RECT card = {cardX, y, cardX + cardW, y + h};
    totalH = y + h - (top - g_scrollFeed);
    y += h + 12;
    if (card.bottom < top || card.top > L.main.bottom) continue;

    roundBox(g, card, 10, theme::panel, theme::line);
    int cx = card.left + 16;
    int cy = card.top + 12;
    circleFill(g, cx + 14, cy + 14, 14, theme::panel2);
    RECT ai = {cx, cy, cx + 28, cy + 28};
    textIn(g, initialsOf(p.authorName.empty() ? p.communityName : p.authorName), ai, theme::text,
           theme::fontSmall(), DT_CENTER | DT_VCENTER | DT_SINGLELINE);
    RECT an = {cx + 38, cy, card.right - 90, cy + 16};
    textIn(g, u2w(p.authorName.empty() ? p.communityName : p.authorName), an, theme::text,
           theme::fontBold(), DT_LEFT | DT_VCENTER | DT_SINGLELINE | DT_END_ELLIPSIS);
    RECT au = {cx + 38, cy + 14, card.right - 90, cy + 30};
    std::string un = p.authorUsername.empty() ? std::string() : ("@" + p.authorUsername);
    textIn(g, u2w(un), au, theme::muted2, theme::fontSmall(),
           DT_LEFT | DT_VCENTER | DT_SINGLELINE | DT_END_ELLIPSIS);
    std::wstring tm = dateOrTime(p.createdAt);
    RECT trr = {card.right - 90, cy, card.right - 16, cy + 28};
    textIn(g, tm, trr, theme::muted2, theme::fontSmall(),
           DT_RIGHT | DT_VCENTER | DT_SINGLELINE);

    int ty = card.top + 52;
    if (!title.empty()) {
      RECT r = {cx, ty, card.right - 16, ty + tr.bottom + 4};
      textIn(g, title, r, theme::text, theme::fontBold(), DT_LEFT | DT_TOP | DT_WORDBREAK);
      ty += tr.bottom + 6;
    }
    RECT r = {cx, ty, card.right - 16, ty + br.bottom + 4};
    textIn(g, body, r, theme::text, theme::fontUI(), DT_LEFT | DT_TOP | DT_WORDBREAK);
    ty += br.bottom + 8;

    if (!p.tags.empty()) {
      int tx = cx;
      for (size_t t = 0; t < p.tags.size() && t < 6; t++) {
        std::wstring tag = L"#" + u2w(p.tags[t]);
        int tw = textWidth(tag, theme::fontSmall()) + 16;
        RECT tg = {tx, ty, tx + tw, ty + 22};
        if (tg.right > card.right - 16) break;
        roundBox(g, tg, 6, theme::bg2, theme::line);
        textIn(g, tag, tg, theme::muted, theme::fontSmall(),
               DT_CENTER | DT_VCENTER | DT_SINGLELINE);
        tx += tw + 6;
      }
      ty += 28;
    }

    // кнопки действий
    RECT hb = {cx, ty, cx + 78, ty + 30};
    bool hotL = (g_hover == hitKey(HK_POST_LIKE, p.id, 0));
    roundBox(g, hb, 6, hotL ? theme::panel2 : theme::bg2, theme::line);
    iconAt(g, "heart", RECT{hb.left + 8, hb.top + 7, hb.left + 24, hb.top + 23},
           p.liked ? theme::danger : theme::muted);
    RECT hc = {hb.left + 26, hb.top, hb.right - 6, hb.bottom};
    textIn(g, countText(p.likes), hc, theme::muted, theme::fontSmall(),
           DT_LEFT | DT_VCENTER | DT_SINGLELINE);
    addHit(HK_POST_LIKE, hb, p.id, 0);

    RECT cb = {hb.right + 8, ty, hb.right + 86, ty + 30};
    bool hotC = (g_hover == hitKey(HK_POST_COMMENT, p.id, 0));
    roundBox(g, cb, 6, hotC ? theme::panel2 : theme::bg2, theme::line);
    iconAt(g, "comment", RECT{cb.left + 8, cb.top + 7, cb.left + 24, cb.top + 23}, theme::muted);
    RECT cc = {cb.left + 26, cb.top, cb.right - 6, cb.bottom};
    textIn(g, countText(p.comments), cc, theme::muted, theme::fontSmall(),
           DT_LEFT | DT_VCENTER | DT_SINGLELINE);
    addHit(HK_POST_COMMENT, cb, p.id, 0);

    RECT lb = {cb.right + 8, ty, cb.right + 42, ty + 30};
    bool hotK = (g_hover == hitKey(HK_POST_LINK, p.id, 0));
    roundBox(g, lb, 6, hotK ? theme::panel2 : theme::bg2, theme::line);
    iconAt(g, "link", RECT{lb.left + 9, lb.top + 7, lb.left + 25, lb.top + 23}, theme::muted);
    addHit(HK_POST_LINK, lb, p.id, 0);

    RECT vb = {lb.right + 12, ty, card.right - 16, ty + 30};
    iconAt(g, "eye", RECT{vb.left, vb.top + 7, vb.left + 16, vb.top + 23}, theme::muted2);
    RECT vt = {vb.left + 22, vb.top, vb.right, vb.bottom};
    textIn(g, countText(p.views), vt, theme::muted2, theme::fontSmall(),
           DT_LEFT | DT_VCENTER | DT_SINGLELINE);
  }
  g.ResetClip();
  g_maxFeed = imax(0, totalH + 24 - viewH);
  if (g_scrollFeed > g_maxFeed) g_scrollFeed = g_maxFeed;
  if (g_scrollFeed < 0) g_scrollFeed = 0;

  if (g_state.posts.empty()) {
    RECT er = {L.main.left, top, L.main.right, top + viewH};
    textIn(g, g_state.loading ? L"Загрузка…" : L"Публикаций пока нет", er, theme::muted2,
           theme::fontUI(), DT_CENTER | DT_VCENTER | DT_SINGLELINE);
  }
}

// ------------------------------------------------------------------ отрисовка: сообщества
static void paintCommunities(Graphics& g, HDC dc, const Layout& L) {
  gdiHLine(dc, L.main.left, L.main.right, theme::headerH - 1, theme::line);
  RECT hr = {L.main.left + theme::pad, 0, L.main.left + 300, theme::headerH};
  textIn(g, L"Сообщества", hr, theme::text, theme::fontTitle(),
         DT_LEFT | DT_VCENTER | DT_SINGLELINE);

  int top = theme::headerH + 8;
  int viewH = L.main.bottom - top;
  int cardX = L.main.left + theme::pad;
  int cardW = imin(760, L.main.right - L.main.left - theme::pad * 2);

  g.SetClip(RectF((REAL)L.main.left, (REAL)top, (REAL)(L.main.right - L.main.left),
                  (REAL)viewH));
  int y = top - g_scrollList;
  int totalH = 0;
  for (size_t i = 0; i < g_state.communities.size(); i++) {
    const Community& c = g_state.communities[i];
    RECT card = {cardX, y, cardX + cardW, y + 104};
    totalH = card.bottom - (top - g_scrollList);
    y += 116;
    if (card.bottom < top || card.top > L.main.bottom) continue;
    roundBox(g, card, 10, theme::panel, theme::line);
    int cx = card.left + 16, cy = card.top + 16;
    circleFill(g, cx + 20, cy + 20, 20, theme::panel2);
    circleStroke(g, cx + 20, cy + 20, 20, theme::line2, 1.0f);
    RECT ai = {cx, cy, cx + 40, cy + 40};
    textIn(g, initialsOf(c.name), ai, theme::text, theme::fontBold(),
           DT_CENTER | DT_VCENTER | DT_SINGLELINE);
    RECT nr = {cx + 52, cy, card.right - 200, cy + 22};
    textIn(g, u2w(c.name), nr, theme::text, theme::fontBold(),
           DT_LEFT | DT_VCENTER | DT_SINGLELINE | DT_END_ELLIPSIS);
    std::wstring kindText = (c.kind == "channel") ? L"канал" : L"группа";
    RECT kr = {cx + 52, cy + 20, card.right - 200, cy + 40};
    textIn(g, kindText + L" · " + countText(c.members) + L" участников", kr, theme::muted,
           theme::fontSmall(), DT_LEFT | DT_VCENTER | DT_SINGLELINE);
    RECT dr = {cx + 52, cy + 42, card.right - 200, card.bottom - 12};
    textIn(g, u2w(c.description), dr, theme::muted, theme::fontSmall(),
           DT_LEFT | DT_TOP | DT_WORDBREAK);

    RECT jb = {card.right - 16 - 130, card.top + 16, card.right - 16, card.top + 50};
    bool hotJ = (g_hover == hitKey(HK_COMMUNITY_JOIN, c.id, 0));
    roundBox(g, jb, 6, c.isMember ? theme::panel2 : (hotJ ? theme::orange2 : theme::orange),
             c.isMember ? theme::line2 : theme::orange);
    textIn(g, c.isMember ? L"Покинуть" : L"Вступить", jb,
           c.isMember ? theme::text : theme::onOrange, theme::fontBold(),
           DT_CENTER | DT_VCENTER | DT_SINGLELINE);
    addHit(HK_COMMUNITY_JOIN, jb, c.id, 0);

    RECT ob = {card.right - 16 - 130, card.top + 58, card.right - 16, card.top + 92};
    bool hotO = (g_hover == hitKey(HK_COMMUNITY_CHAT, c.id, 0));
    roundBox(g, ob, 6, hotO ? theme::panel2 : theme::bg2, theme::line);
    textIn(g, L"Открыть чат", ob, theme::text, theme::fontUI(),
           DT_CENTER | DT_VCENTER | DT_SINGLELINE);
    addHit(HK_COMMUNITY_CHAT, ob, c.id, 0);
  }
  g.ResetClip();
  g_maxList = imax(0, totalH + 16 - viewH);
  if (g_scrollList > g_maxList) g_scrollList = g_maxList;
  if (g_scrollList < 0) g_scrollList = 0;

  if (g_state.communities.empty()) {
    RECT er = {L.main.left, top, L.main.right, top + viewH};
    textIn(g, g_state.loading ? L"Загрузка…" : L"Сообществ пока нет", er, theme::muted2,
           theme::fontUI(), DT_CENTER | DT_VCENTER | DT_SINGLELINE);
  }
}

// ------------------------------------------------------------------ отрисовка: истории
static void paintStories(Graphics& g, HDC dc, const Layout& L) {
  gdiHLine(dc, L.main.left, L.main.right, theme::headerH - 1, theme::line);
  RECT hr = {L.main.left + theme::pad, 0, L.main.left + 300, theme::headerH};
  textIn(g, L"Истории", hr, theme::text, theme::fontTitle(),
         DT_LEFT | DT_VCENTER | DT_SINGLELINE);

  RECT ab = {L.main.right - theme::pad - 130, (theme::headerH - 34) / 2,
             L.main.right - theme::pad, (theme::headerH - 34) / 2 + 34};
  bool hotA = (g_hover == hitKey(HK_ACTION, 0, ACT_STORY_ADD));
  roundBox(g, ab, 6, hotA ? theme::orange2 : theme::orange, theme::orange);
  textIn(g, L"История", ab, theme::onOrange, theme::fontBold(),
         DT_CENTER | DT_VCENTER | DT_SINGLELINE);
  addHit(HK_ACTION, ab, 0, ACT_STORY_ADD);

  int top = theme::headerH + 16;
  int viewH = L.main.bottom - top;
  int availW = L.main.right - L.main.left - theme::pad * 2;
  int cols = imax(2, imin(5, availW / 190));
  int gap = 12;
  int cw = (availW - gap * (cols - 1)) / cols;
  int ch = 172;

  g.SetClip(RectF((REAL)L.main.left, (REAL)top, (REAL)(L.main.right - L.main.left),
                  (REAL)viewH));
  int totalH = 0;
  for (size_t i = 0; i < g_state.stories.size(); i++) {
    const StoryGroup& s = g_state.stories[i];
    int col = (int)i % cols, rowi = (int)i / cols;
    int x = L.main.left + theme::pad + col * (cw + gap);
    int y = top + rowi * (ch + gap) - g_scrollList;
    RECT card = {x, y, x + cw, y + ch};
    totalH = card.bottom - (top - g_scrollList);
    if (card.bottom < top || card.top > L.main.bottom) continue;
    bool hot = (g_hover == hitKey(HK_STORY, s.id, 0));
    roundBox(g, card, 10, hot ? theme::panel2 : theme::panel, theme::line);
    int ccx = x + cw / 2, ccy = y + 58;
    circleStroke(g, ccx, ccy, 36, s.hasUnseen ? theme::orange : theme::line2, 2.0f);
    circleFill(g, ccx, ccy, 31, theme::panel2);
    RECT ai = {ccx - 31, ccy - 31, ccx + 31, ccy + 31};
    textIn(g, initialsOf(s.name), ai, theme::text, theme::fontTitle(),
           DT_CENTER | DT_VCENTER | DT_SINGLELINE);
    RECT nr = {x + 8, y + 106, x + cw - 8, y + 128};
    textIn(g, u2w(s.name), nr, theme::text, theme::fontBold(),
           DT_CENTER | DT_VCENTER | DT_SINGLELINE | DT_END_ELLIPSIS);
    std::wstring sub = s.count > 0 ? (countText(s.count) + L" истории") : u2w(s.caption);
    RECT sr = {x + 8, y + 128, x + cw - 8, y + 148};
    textIn(g, sub, sr, theme::muted, theme::fontSmall(),
           DT_CENTER | DT_VCENTER | DT_SINGLELINE | DT_END_ELLIPSIS);
    addHit(HK_STORY, card, s.id, 0);
  }
  g.ResetClip();
  g_maxList = imax(0, totalH + 16 - viewH);
  if (g_scrollList > g_maxList) g_scrollList = g_maxList;
  if (g_scrollList < 0) g_scrollList = 0;

  if (g_state.stories.empty()) {
    RECT er = {L.main.left, top, L.main.right, top + viewH};
    textIn(g, g_state.loading ? L"Загрузка…" : L"Историй пока нет", er, theme::muted2,
           theme::fontUI(), DT_CENTER | DT_VCENTER | DT_SINGLELINE);
  }
}

// ------------------------------------------------------------------ отрисовка: профиль
static void paintProfile(Graphics& g, HDC dc, const Layout& L) {
  gdiHLine(dc, L.main.left, L.main.right, theme::headerH - 1, theme::line);
  RECT hr = {L.main.left + theme::pad, 0, L.main.left + 300, theme::headerH};
  textIn(g, L"Настройки профиля", hr, theme::text, theme::fontTitle(),
         DT_LEFT | DT_VCENTER | DT_SINGLELINE);

  int cardW = imin(760, L.main.right - L.main.left - theme::pad * 2);
  int cardX = L.main.left + theme::pad;
  int cardY = theme::headerH + 16 - g_scrollProfile;
  int cardH = 360;
  RECT card = {cardX, cardY, cardX + cardW, cardY + cardH};
  g_maxProfile = imax(0, cardH + 32 - (L.main.bottom - theme::headerH));
  if (g_scrollProfile > g_maxProfile) g_scrollProfile = g_maxProfile;
  if (g_scrollProfile < 0) g_scrollProfile = 0;

  g.SetClip(RectF((REAL)L.main.left, (REAL)theme::headerH,
                  (REAL)(L.main.right - L.main.left),
                  (REAL)(L.main.bottom - theme::headerH)));
  roundBox(g, card, 10, theme::panel, theme::line);

  std::string nm = g_state.me.displayName.empty() ? g_state.me.username : g_state.me.displayName;
  if (nm.empty()) nm = "Гость";
  int ax = card.left + 24, ay = card.top + 24;
  circleFill(g, ax + 36, ay + 36, 36, theme::panel2);
  circleStroke(g, ax + 36, ay + 36, 36, theme::line2, 1.0f);
  RECT ai = {ax, ay, ax + 72, ay + 72};
  textIn(g, initialsOf(nm), ai, theme::text, theme::fontTitle(),
         DT_CENTER | DT_VCENTER | DT_SINGLELINE);

  RECT nr = {ax + 92, ay + 6, card.right - 24, ay + 32};
  textIn(g, u2w(nm), nr, theme::text, theme::fontTitle(),
         DT_LEFT | DT_VCENTER | DT_SINGLELINE | DT_END_ELLIPSIS);
  RECT ur = {ax + 92, ay + 32, card.right - 24, ay + 52};
  if (!g_state.me.username.empty())
    textIn(g, L"@" + u2w(g_state.me.username), ur, theme::muted, theme::fontUI(),
           DT_LEFT | DT_VCENTER | DT_SINGLELINE);
  RECT orr = {ax + 92, ay + 52, card.right - 24, ay + 72};
  textIn(g, u2w(g_state.me.orangeId), orr, theme::muted, theme::fontMono(),
         DT_LEFT | DT_VCENTER | DT_SINGLELINE);

  RECT br = {card.left + 24, card.top + 112, card.right - 24, card.top + 172};
  textIn(g, g_state.me.bio.empty() ? L"Биография не заполнена" : u2w(g_state.me.bio), br,
         g_state.me.bio.empty() ? theme::muted2 : theme::text, theme::fontUI(),
         DT_LEFT | DT_TOP | DT_WORDBREAK);

  // счётчики
  struct Counter { const wchar_t* label; long long value; };
  Counter cs[3] = {{L"подписчики", g_state.me.followers},
                   {L"подписки", g_state.me.following},
                   {L"публикации", g_state.me.posts}};
  int cxp = card.left + 24;
  for (int i = 0; i < 3; i++) {
    RECT vr = {cxp, card.top + 176, cxp + 150, card.top + 202};
    textIn(g, countText(cs[i].value), vr, theme::text, theme::fontTitle(),
           DT_LEFT | DT_VCENTER | DT_SINGLELINE);
    RECT lr = {cxp, card.top + 200, cxp + 150, card.top + 218};
    textIn(g, cs[i].label, lr, theme::muted2, theme::fontSmall(),
           DT_LEFT | DT_VCENTER | DT_SINGLELINE);
    cxp += 150;
  }

  RECT rb = {card.left + 24, card.top + 226, card.left + 24 + 150, card.top + 226 + 32};
  bool hotR = (g_hover == hitKey(HK_ACTION, 0, ACT_REFRESH));
  roundBox(g, rb, 6, hotR ? theme::orange2 : theme::orange, theme::orange);
  textIn(g, L"Обновить", rb, theme::onOrange, theme::fontBold(),
         DT_CENTER | DT_VCENTER | DT_SINGLELINE);
  addHit(HK_ACTION, rb, 0, ACT_REFRESH);

  RECT lb = {rb.right + 10, rb.top, rb.right + 10 + 130, rb.bottom};
  bool hotL = (g_hover == hitKey(HK_ACTION, 0, ACT_LOGOUT));
  roundBox(g, lb, 6, hotL ? theme::panel2 : theme::bg2, theme::danger);
  iconAt(g, "logout", RECT{lb.left + 10, lb.top + 8, lb.left + 26, lb.top + 24}, theme::danger);
  RECT lt = {lb.left + 32, lb.top, lb.right - 6, lb.bottom};
  textIn(g, L"Выйти", lt, theme::danger, theme::fontBold(),
         DT_LEFT | DT_VCENTER | DT_SINGLELINE);
  addHit(HK_ACTION, lb, 0, ACT_LOGOUT);

  RECT sl = {card.left + 24, card.top + 268 - 22, card.right - 24, card.top + 268 - 6};
  textIn(g, L"АДРЕС СЕРВЕРА", sl, theme::muted2, theme::fontSmall(),
         DT_LEFT | DT_VCENTER | DT_SINGLELINE);
  roundBox(g, L.serverEdit, 6, theme::bg2, theme::line);

  RECT sh = {card.left + 24, card.top + 308, card.right - 24, card.top + 328};
  textIn(g, L"Enter — применить адрес сервера", sh, theme::muted2, theme::fontSmall(),
         DT_LEFT | DT_VCENTER | DT_SINGLELINE);

  COLORREF cc = g_state.realtime ? theme::ok : theme::danger;
  circleFill(g, card.left + 30, card.top + 342, 4, cc);
  RECT rr = {card.left + 42, card.top + 332, card.right - 24, card.top + 352};
  textIn(g, g_state.realtime ? L"realtime подключён" : L"realtime не подключён", rr,
         theme::muted, theme::fontSmall(), DT_LEFT | DT_VCENTER | DT_SINGLELINE);
  g.ResetClip();
}

// ------------------------------------------------------------------ отрисовка: тост
static void paintToast(Graphics& g, HDC dc, const Layout& L) {
  if (g_toast.empty()) return;
  int maxW = 440;
  RECT mr;
  measureText(g_toast, theme::fontUI(), maxW - 44, mr);
  int w = imin(maxW, mr.right + 44);
  if (w < 220) w = 220;
  int h = imax(44, mr.bottom + 24);
  RECT r = {L.w - theme::pad - w, L.status.top - 12 - h, L.w - theme::pad, L.status.top - 12};
  roundBox(g, r, 8, theme::panel2, theme::line2);
  RECT bar = {r.left, r.top + 4, r.left + 3, r.bottom - 4};
  roundFill(g, bar, 2, g_toastErr ? theme::danger : theme::orange);
  RECT tr = {r.left + 16, r.top, r.right - 12, r.bottom};
  textIn(g, g_toast, tr, theme::text, theme::fontUI(), DT_LEFT | DT_VCENTER | DT_WORDBREAK);
}

// ------------------------------------------------------------------ компоновка дочерних окон
static void layoutChildren() {
  if (!g_hwnd) return;
  Layout L = computeLayout();
  auto place = [](HWND h, const RECT& r, bool visible) {
    if (!h) return;
    if (visible) {
      MoveWindow(h, r.left, r.top, r.right - r.left, r.bottom - r.top, TRUE);
      ShowWindow(h, SW_SHOWNA);
    } else {
      ShowWindow(h, SW_HIDE);
    }
  };
  place(g_searchEdit, L.searchBox, L.showSearch);
  if (L.showSearch && g_searchEdit) {
    MoveWindow(g_searchEdit, L.searchBox.left + 28, L.searchBox.top + 5,
               L.searchBox.right - L.searchBox.left - 60, L.searchBox.bottom - L.searchBox.top - 10,
               TRUE);
  }
  place(g_msgEdit, L.msgEdit, L.showMsg);
  place(g_composerEdit, L.composerEdit, L.showComposer);
  place(g_serverEdit, L.serverEdit, L.showServer);
}

// ------------------------------------------------------------------ отрисовка кадра
static void paintFrame(HDC dc, const RECT& rc) {
  Layout L = computeLayout();
  g_hits.clear();
  Graphics g(dc);
  g.SetSmoothingMode(SmoothingModeAntiAlias);
  g.SetPixelOffsetMode(PixelOffsetModeHalf);
  gdiFill(dc, rc, theme::bg);

  if (L.hasList) paintListPanel(g, dc, L);

  switch (g_state.section) {
    case SEC_CHATS:
      paintChatMessages(g, dc, L);
      break;
    case SEC_FEED:
      paintFeed(g, dc, L);
      break;
    case SEC_COMMUNITIES:
      paintCommunities(g, dc, L);
      break;
    case SEC_STORIES:
      paintStories(g, dc, L);
      break;
    case SEC_PROFILE:
      paintProfile(g, dc, L);
      break;
    default:
      break;
  }

  paintSidebar(g, dc, L);
  paintStatusBar(g, dc, L);
  paintToast(g, dc, L);
}

// ------------------------------------------------------------------ действия
static void doSendMessage() {
  if (!g_msgEdit) return;
  std::wstring t = getEditTextW(g_msgEdit);
  if (t.empty()) return;
  if (g_cb.onSendMessage) g_cb.onSendMessage(w2u(t));
  SetWindowTextW(g_msgEdit, L"");
  g_msgsToBottom = true;
  invalidate();
}

static void doCreatePost() {
  if (!g_composerEdit) return;
  std::wstring t = getEditTextW(g_composerEdit);
  if (t.empty()) return;
  if (g_cb.onCreatePost) g_cb.onCreatePost(w2u(t));
  SetWindowTextW(g_composerEdit, L"");
  invalidate();
}

static void doSearch() {
  if (!g_searchEdit) return;
  std::wstring t = getEditTextW(g_searchEdit);
  g_state.search = w2u(t);
  if (g_cb.onSearch) g_cb.onSearch(g_state.search);
  invalidate();
}

static void doSearchClear() {
  if (g_searchEdit) SetWindowTextW(g_searchEdit, L"");
  g_state.search.clear();
  if (g_cb.onSearchCleared) g_cb.onSearchCleared();
  invalidate();
}

static void doServerChanged() {
  if (!g_serverEdit) return;
  std::wstring t = getEditTextW(g_serverEdit);
  if (t.empty()) return;
  if (g_cb.onServerChanged) g_cb.onServerChanged(w2u(t));
  setStatus("Адрес сервера обновлён");
}

static void doAction(int act, long long id) {
  switch (act) {
    case ACT_NEW_CHAT:
      if (g_cb.onNewChat) g_cb.onNewChat();
      break;
    case ACT_REFRESH:
      if (g_cb.onRefresh) g_cb.onRefresh();
      break;
    case ACT_LOGOUT:
      if (g_cb.onLogout) g_cb.onLogout();
      break;
    case ACT_ATTACH:
      if (g_cb.onAttach) g_cb.onAttach();
      break;
    case ACT_SEND:
      doSendMessage();
      break;
    case ACT_PUBLISH:
      doCreatePost();
      break;
    case ACT_SEARCH_CLEAR:
      doSearchClear();
      break;
    case ACT_STORY_ADD:
      if (g_cb.onCreateStory) g_cb.onCreateStory();
      break;
    case ACT_SERVER_SAVE:
      doServerChanged();
      break;
    case ACT_CHAT_SEARCH:
      if (g_searchEdit) {
        SetFocus(g_searchEdit);
        SetWindowTextW(g_searchEdit, L"");
      }
      break;
    default:
      break;
  }
  (void)id;
}

static void doHotkeySection(int idx) {
  static const int secs[5] = {SEC_CHATS, SEC_FEED, SEC_COMMUNITIES, SEC_STORIES, SEC_PROFILE};
  if (idx < 0 || idx > 4) return;
  openSection(secs[idx]);
}

// ------------------------------------------------------------------ модальное окно комментария
static void commentFinish(bool ok) {
  if (!g_commentWnd) return;
  std::wstring t = getEditTextW(g_commentEdit);
  if (ok && !t.empty() && g_cb.onCommentPost) g_cb.onCommentPost(g_commentPostId, w2u(t));
  DestroyWindow(g_commentWnd);
}

static LRESULT CALLBACK commentProc(HWND hwnd, UINT msg, WPARAM wp, LPARAM lp) {
  switch (msg) {
    case WM_CREATE: {
      HFONT f = theme::fontUI();
      g_commentEdit = CreateWindowExW(0, L"EDIT", L"",
                                      WS_CHILD | WS_VISIBLE | WS_BORDER | ES_MULTILINE |
                                          ES_AUTOVSCROLL | ES_WANTRETURN | WS_TABSTOP,
                                      16, 16, 388, 92, hwnd, (HMENU)(INT_PTR)IDC_COMMENT, g_inst,
                                      nullptr);
      SendMessageW(g_commentEdit, WM_SETFONT, (WPARAM)f, TRUE);
      HWND ok = CreateWindowExW(0, L"BUTTON", L"Отправить", WS_CHILD | WS_VISIBLE | BS_OWNERDRAW,
                                248, 118, 156, 32, hwnd, (HMENU)(INT_PTR)IDC_COMMENT_OK, g_inst,
                                nullptr);
      SendMessageW(ok, WM_SETFONT, (WPARAM)f, TRUE);
      HWND cn = CreateWindowExW(0, L"BUTTON", L"Отмена", WS_CHILD | WS_VISIBLE | BS_OWNERDRAW, 166,
                                118, 76, 32, hwnd, (HMENU)(INT_PTR)IDC_COMMENT_CANCEL, g_inst,
                                nullptr);
      SendMessageW(cn, WM_SETFONT, (WPARAM)f, TRUE);
      SetFocus(g_commentEdit);
      return 0;
    }
    case WM_PAINT: {
      PAINTSTRUCT ps;
      HDC dc = BeginPaint(hwnd, &ps);
      RECT rc;
      GetClientRect(hwnd, &rc);
      gdiFill(dc, rc, theme::panel);
      Graphics g(dc);
      g.SetSmoothingMode(SmoothingModeAntiAlias);
      RECT t = {16, 0, rc.right - 16, 14};
      EndPaint(hwnd, &ps);
      (void)t;
      return 0;
    }
    case WM_DRAWITEM: {
      LPDRAWITEMSTRUCT d = (LPDRAWITEMSTRUCT)lp;
      bool primary = (d->CtlID == IDC_COMMENT_OK);
      HBRUSH b = CreateSolidBrush(primary ? theme::orange : theme::panel2);
      FillRect(d->hDC, &d->rcItem, b);
      DeleteObject(b);
      if (!primary) {
        HBRUSH fr = CreateSolidBrush(theme::line2);
        FrameRect(d->hDC, &d->rcItem, fr);
        DeleteObject(fr);
      }
      wchar_t text[64] = {0};
      GetWindowTextW(d->hwndItem, text, 63);
      SetBkMode(d->hDC, TRANSPARENT);
      SetTextColor(d->hDC, primary ? theme::onOrange : theme::text);
      SelectObject(d->hDC, theme::fontBold());
      RECT r = d->rcItem;
      DrawTextW(d->hDC, text, -1, &r, DT_CENTER | DT_VCENTER | DT_SINGLELINE);
      return TRUE;
    }
    case WM_CTLCOLOREDIT:
    case WM_CTLCOLORSTATIC: {
      HDC dc = (HDC)wp;
      SetTextColor(dc, theme::text);
      SetBkColor(dc, theme::bg2);
      static HBRUSH br = CreateSolidBrush(theme::bg2);
      return (LRESULT)br;
    }
    case WM_COMMAND:
      if (LOWORD(wp) == IDC_COMMENT_OK) {
        commentFinish(true);
        return 0;
      }
      if (LOWORD(wp) == IDC_COMMENT_CANCEL) {
        commentFinish(false);
        return 0;
      }
      break;
    case WM_CLOSE:
      commentFinish(false);
      return 0;
    case WM_DESTROY:
      if (g_hwnd) {
        EnableWindow(g_hwnd, TRUE);
        SetForegroundWindow(g_hwnd);
      }
      g_commentEdit = nullptr;
      g_commentWnd = nullptr;
      return 0;
  }
  return DefWindowProcW(hwnd, msg, wp, lp);
}

static void openCommentDialog(long long postId) {
  if (g_commentWnd) {
    SetForegroundWindow(g_commentWnd);
    return;
  }
  static bool reg = false;
  if (!reg) {
    WNDCLASSEXW wc;
    ZeroMemory(&wc, sizeof(wc));
    wc.cbSize = sizeof(wc);
    wc.lpfnWndProc = commentProc;
    wc.hInstance = g_inst;
    wc.hCursor = LoadCursor(nullptr, IDC_ARROW);
    wc.hbrBackground = nullptr;
    wc.lpszClassName = L"OrangeMComment";
    RegisterClassExW(&wc);
    reg = true;
  }
  g_commentPostId = postId;
  RECT pr;
  GetWindowRect(g_hwnd, &pr);
  int w = 420, h = 190;
  int x = pr.left + ((pr.right - pr.left) - w) / 2;
  int y = pr.top + ((pr.bottom - pr.top) - h) / 2;
  g_commentWnd = CreateWindowExW(WS_EX_TOOLWINDOW, L"OrangeMComment", L"Комментарий к публикации",
                                 WS_POPUP | WS_CAPTION | WS_SYSMENU, x, y, w, h, g_hwnd, nullptr,
                                 g_inst, nullptr);
  if (!g_commentWnd) return;
  EnableWindow(g_hwnd, FALSE);
  ShowWindow(g_commentWnd, SW_SHOW);
  UpdateWindow(g_commentWnd);
  if (g_commentEdit) SetFocus(g_commentEdit);
}

// ------------------------------------------------------------------ подкласс EDIT-полей
static LRESULT CALLBACK editProc(HWND hwnd, UINT msg, WPARAM wp, LPARAM lp) {
  if (msg == WM_GETDLGCODE) return DLGC_WANTALLKEYS | DLGC_WANTMESSAGE;
  if (msg == WM_KEYDOWN) {
    bool ctrl = (GetKeyState(VK_CONTROL) & 0x8000) != 0;
    if (ctrl && wp >= '1' && wp <= '5') {
      doHotkeySection((int)(wp - '1'));
      return 0;
    }
    if (wp == VK_F5) {
      doAction(ACT_REFRESH, 0);
      return 0;
    }
    if (wp == VK_ESCAPE) {
      doSearchClear();
      if (g_hwnd) SetFocus(g_hwnd);
      return 0;
    }
    if (wp == VK_RETURN) {
      if (hwnd == g_searchEdit) {
        doSearch();
        return 0;
      }
      if (hwnd == g_serverEdit) {
        doServerChanged();
        return 0;
      }
      if (hwnd == g_msgEdit) {
        doSendMessage();
        return 0;
      }
      if (hwnd == g_composerEdit) {
        if (ctrl) doCreatePost();
        return 0;
      }
      if (hwnd == g_commentEdit) {
        commentFinish(true);
        return 0;
      }
    }
  }
  std::map<HWND, WNDPROC>::iterator it = g_oldProcs.find(hwnd);
  if (it != g_oldProcs.end() && it->second) return CallWindowProc(it->second, hwnd, msg, wp, lp);
  return DefWindowProcW(hwnd, msg, wp, lp);
}

static void subclassEdit(HWND h) {
  if (!h) return;
  WNDPROC old = (WNDPROC)SetWindowLongPtrW(h, GWLP_WNDPROC, (LONG_PTR)editProc);
  g_oldProcs[h] = old;
}

// ------------------------------------------------------------------ обработка сообщений
static void updateHover(POINT p) {
  const Hit* h = hitAt(p);
  long long key = h ? hitKey(h->kind, h->id, h->act) : 0;
  if (key != g_hover) {
    g_hover = key;
    invalidate();
  }
}

static bool handleInternal(HWND hwnd, UINT msg, WPARAM wp, LPARAM lp, LRESULT& result) {
  switch (msg) {
    case WM_ERASEBKGND:
      result = 1;
      return true;

    case WM_SIZE:
      layoutChildren();
      invalidate();
      result = 0;
      return true;

    case WM_MOVE:
    case WM_ACTIVATE:
      return false;

    case WM_PAINT: {
      PAINTSTRUCT ps;
      HDC dc = BeginPaint(hwnd, &ps);
      RECT rc;
      GetClientRect(hwnd, &rc);
      HDC mem = CreateCompatibleDC(dc);
      HBITMAP bmp = CreateCompatibleBitmap(dc, rc.right > 0 ? rc.right : 1,
                                           rc.bottom > 0 ? rc.bottom : 1);
      HBITMAP old = (HBITMAP)SelectObject(mem, bmp);
      paintFrame(mem, rc);
      BitBlt(dc, 0, 0, rc.right, rc.bottom, mem, 0, 0, SRCCOPY);
      SelectObject(mem, old);
      DeleteObject(bmp);
      DeleteDC(mem);
      EndPaint(hwnd, &ps);
      result = 0;
      return true;
    }

    case WM_MOUSEMOVE: {
      if (!g_tracking) {
        TRACKMOUSEEVENT tme;
        ZeroMemory(&tme, sizeof(tme));
        tme.cbSize = sizeof(tme);
        tme.dwFlags = TME_LEAVE;
        tme.hwndTrack = hwnd;
        TrackMouseEvent(&tme);
        g_tracking = true;
      }
      POINT p;
      p.x = (short)LOWORD(lp);
      p.y = (short)HIWORD(lp);
      updateHover(p);
      result = 0;
      return true;
    }

    case WM_MOUSELEAVE:
      g_tracking = false;
      if (g_hover != 0) {
        g_hover = 0;
        invalidate();
      }
      result = 0;
      return true;

    case WM_SETCURSOR: {
      if (LOWORD(lp) == HTCLIENT) {
        POINT p;
        GetCursorPos(&p);
        ScreenToClient(hwnd, &p);
        SetCursor(LoadCursor(nullptr, hitAt(p) ? IDC_HAND : IDC_ARROW));
        result = TRUE;
        return true;
      }
      return false;
    }

    case WM_LBUTTONDOWN: {
      POINT p;
      p.x = (short)LOWORD(lp);
      p.y = (short)HIWORD(lp);
      const Hit* h = hitAt(p);
      if (!h) break;
      HitKind k = h->kind;
      long long id = h->id;
      int act = h->act;
      switch (k) {
        case HK_SECTION:
          openSection((int)id);
          break;
        case HK_CHAT:
          if (g_cb.onChatSelected) g_cb.onChatSelected(id);
          g_msgsToBottom = true;
          break;
        case HK_COMMUNITY_CARD:
          for (const auto& c : g_state.communities) {
            if (c.id == id) {
              if (g_cb.onToggleCommunity) g_cb.onToggleCommunity(id, !c.isMember);
              break;
            }
          }
          break;
        case HK_COMMUNITY_JOIN: {
          bool join = true;
          for (const auto& c : g_state.communities)
            if (c.id == id) join = !c.isMember;
          if (g_cb.onToggleCommunity) g_cb.onToggleCommunity(id, join);
          break;
        }
        case HK_COMMUNITY_CHAT: {
          long long chatId = 0;
          for (const auto& c : g_state.communities)
            if (c.id == id) chatId = c.chatId;
          if (chatId && g_cb.onOpenCommunityChat) g_cb.onOpenCommunityChat(chatId);
          break;
        }
        case HK_POST_LIKE: {
          bool liked = false;
          for (const auto& p0 : g_state.posts)
            if (p0.id == id) liked = p0.liked;
          if (g_cb.onLikePost) g_cb.onLikePost(id, !liked);
          break;
        }
        case HK_POST_COMMENT:
          openCommentDialog(id);
          break;
        case HK_POST_LINK: {
          std::wstring link = L"orangem://post/" + std::to_wstring(id);
          if (OpenClipboard(hwnd)) {
            EmptyClipboard();
            size_t bytes = (link.size() + 1) * sizeof(wchar_t);
            HGLOBAL mem = GlobalAlloc(GMEM_MOVEABLE, bytes);
            if (mem) {
              void* dst = GlobalLock(mem);
              if (dst) {
                memcpy(dst, link.c_str(), bytes);
                GlobalUnlock(mem);
                SetClipboardData(CF_UNICODETEXT, mem);
              }
            }
            CloseClipboard();
          }
          showToast("Ссылка на публикацию скопирована");
          break;
        }
        case HK_STORY:
          if (g_cb.onViewStory) g_cb.onViewStory(id);
          break;
        case HK_ACTION:
          doAction(act, id);
          break;
        default:
          break;
      }
      result = 0;
      return true;
    }

    case WM_MOUSEWHEEL: {
      int delta = GET_WHEEL_DELTA_WPARAM(wp);
      POINT p;
      p.x = (short)LOWORD(lp);
      p.y = (short)HIWORD(lp);
      ScreenToClient(hwnd, &p);
      int step = (delta / WHEEL_DELTA) * 60;
      if (g_state.section == SEC_CHATS) {
        if (p.x >= theme::sidebarW + theme::listW) {
          g_scrollMsgs -= step;
          if (g_scrollMsgs < 0) g_scrollMsgs = 0;
          if (g_scrollMsgs > g_maxMsgs) g_scrollMsgs = g_maxMsgs;
        } else {
          g_scrollList -= step;
          if (g_scrollList < 0) g_scrollList = 0;
          if (g_scrollList > g_maxList) g_scrollList = g_maxList;
        }
      } else if (g_state.section == SEC_FEED) {
        g_scrollFeed -= step;
        if (g_scrollFeed < 0) g_scrollFeed = 0;
        if (g_scrollFeed > g_maxFeed) g_scrollFeed = g_maxFeed;
      } else if (g_state.section == SEC_PROFILE) {
        g_scrollProfile -= step;
        if (g_scrollProfile < 0) g_scrollProfile = 0;
        if (g_scrollProfile > g_maxProfile) g_scrollProfile = g_maxProfile;
      } else {
        g_scrollList -= step;
        if (g_scrollList < 0) g_scrollList = 0;
        if (g_scrollList > g_maxList) g_scrollList = g_maxList;
      }
      invalidate();
      result = 0;
      return true;
    }

    case WM_KEYDOWN: {
      bool ctrl = (GetKeyState(VK_CONTROL) & 0x8000) != 0;
      if (ctrl && wp >= '1' && wp <= '5') {
        doHotkeySection((int)(wp - '1'));
        result = 0;
        return true;
      }
      if (wp == VK_F5) {
        doAction(ACT_REFRESH, 0);
        result = 0;
        return true;
      }
      if (wp == VK_ESCAPE) {
        doSearchClear();
        result = 0;
        return true;
      }
      return false;
    }

    case WM_COMMAND: {
      int id = LOWORD(wp);
      if (id == IDC_SEARCH && HIWORD(wp) == EN_CHANGE) {
        invalidate();
        result = 0;
        return true;
      }
      if (id == IDCANCEL) {
        doSearchClear();
        result = 0;
        return true;
      }
      return false;
    }

    case WM_CTLCOLOREDIT:
    case WM_CTLCOLORSTATIC: {
      HDC dc = (HDC)wp;
      SetTextColor(dc, msg == WM_CTLCOLOREDIT ? theme::text : theme::muted);
      SetBkColor(dc, theme::bg2);
      static HBRUSH br = CreateSolidBrush(theme::bg2);
      result = (LRESULT)br;
      return true;
    }

    case WM_TIMER:
      if (wp == IDT_TOAST) {
        KillTimer(hwnd, IDT_TOAST);
        g_toast.clear();
        invalidate();
        result = 0;
        return true;
      }
      return false;

    default:
      return false;
  }
  result = 0;
  return true;
}

static LRESULT CALLBACK wndProc(HWND hwnd, UINT msg, WPARAM wp, LPARAM lp) {
  LRESULT r = 0;
  if (g_app && g_app(hwnd, msg, wp, lp, r)) return r;
  if (handleInternal(hwnd, msg, wp, lp, r)) return r;
  return DefWindowProcW(hwnd, msg, wp, lp);
}

// ------------------------------------------------------------------ публичный интерфейс
void setCallbacks(UiCallbacks cb) { g_cb = cb; }

void setAppMessageHandler(std::function<bool(HWND, UINT, WPARAM, LPARAM, LRESULT&)> fn) {
  g_app = fn;
}

void setState(const UiState& s) {
  UiState old = g_state;
  g_state = s;
  if (g_state.activeChat != old.activeChat || g_state.msgs.size() > old.msgs.size()) {
    if (g_state.activeChat != 0) g_msgsToBottom = true;
  }
  if (g_searchEdit && GetFocus() != g_searchEdit) {
    std::wstring cur = getEditTextW(g_searchEdit);
    if (w2u(cur) != g_state.search) SetWindowTextW(g_searchEdit, u2w(g_state.search).c_str());
  }
  if (g_serverEdit && GetFocus() != g_serverEdit) {
    std::wstring cur = getEditTextW(g_serverEdit);
    if (w2u(cur) != g_state.serverUrl) SetWindowTextW(g_serverEdit, u2w(g_state.serverUrl).c_str());
  }
  if (g_state.section != old.section) {
    g_scrollList = 0;
    g_hover = 0;
    layoutChildren();
  }
}

UiState& state() { return g_state; }

void invalidate() {
  if (g_hwnd) InvalidateRect(g_hwnd, nullptr, FALSE);
}

HWND mainWindow() { return g_hwnd; }

bool handleMessage(HWND hwnd, UINT msg, WPARAM wp, LPARAM lp, LRESULT& result) {
  if (hwnd != g_hwnd) return false;
  return handleInternal(hwnd, msg, wp, lp, result);
}

HWND createMainWindow(HINSTANCE inst, const std::wstring& title) {
  g_inst = inst;
  const wchar_t* cls = L"OrangeMMain";
  static bool reg = false;
  if (!reg) {
    WNDCLASSEXW wc;
    ZeroMemory(&wc, sizeof(wc));
    wc.cbSize = sizeof(wc);
    wc.style = CS_HREDRAW | CS_VREDRAW;
    wc.lpfnWndProc = wndProc;
    wc.hInstance = inst;
    wc.hCursor = LoadCursor(nullptr, IDC_ARROW);
    wc.hbrBackground = nullptr;
    wc.lpszClassName = cls;
    wc.hIcon = LoadIconW(inst, MAKEINTRESOURCEW(101));
    wc.hIconSm = wc.hIcon;
    RegisterClassExW(&wc);
    reg = true;
  }
  if (g_dc) DeleteDC(g_dc);
  g_dc = CreateCompatibleDC(nullptr);

  g_hwnd = CreateWindowExW(0, cls, title.c_str(), WS_OVERLAPPEDWINDOW, CW_USEDEFAULT,
                           CW_USEDEFAULT, 1280, 800, nullptr, nullptr, inst, nullptr);
  if (!g_hwnd) return nullptr;

  loadLogo();

  HFONT f = theme::fontUI();
  auto mkEdit = [&](DWORD style, int id) {
    HWND h = CreateWindowExW(0, L"EDIT", L"", WS_CHILD | style, 0, 0, 10, 10, g_hwnd,
                             (HMENU)(INT_PTR)id, inst, nullptr);
    SendMessageW(h, WM_SETFONT, (WPARAM)f, TRUE);
    subclassEdit(h);
    return h;
  };
  g_searchEdit = mkEdit(ES_AUTOHSCROLL, IDC_SEARCH);
  g_msgEdit = mkEdit(ES_AUTOHSCROLL, IDC_MSG);
  g_composerEdit = mkEdit(ES_MULTILINE | ES_AUTOVSCROLL | ES_WANTRETURN | WS_VSCROLL, IDC_COMPOSER);
  g_serverEdit = mkEdit(ES_AUTOHSCROLL, IDC_SERVER);

  layoutChildren();
  return g_hwnd;
}

void destroyMainWindow() {
  if (g_hwnd) {
    KillTimer(g_hwnd, IDT_TOAST);
    g_hwnd = nullptr;
  }
  if (g_commentWnd) {
    DestroyWindow(g_commentWnd);
    g_commentWnd = nullptr;
  }
  g_hits.clear();
  g_hover = 0;
  if (g_logo) {
    delete g_logo;
    g_logo = nullptr;
  }
  if (g_dc) {
    DeleteDC(g_dc);
    g_dc = nullptr;
  }
  if (g_fillBrush) {
    DeleteObject(g_fillBrush);
    g_fillBrush = nullptr;
    g_fillColor = 0xFFFFFFFF;
  }
}

std::wstring messageInputText() { return getEditTextW(g_msgEdit); }

void setMessageInputText(const std::wstring& text) {
  if (g_msgEdit) SetWindowTextW(g_msgEdit, text.c_str());
}

void focusMessageInput() {
  if (g_msgEdit) SetFocus(g_msgEdit);
}

void scrollChatToBottom() {
  g_msgsToBottom = true;
  invalidate();
}

void showToast(const std::string& text) {
  g_toast = u2w(text);
  g_toastErr = looksLikeError(text);
  if (g_hwnd) {
    KillTimer(g_hwnd, IDT_TOAST);
    SetTimer(g_hwnd, IDT_TOAST, 2500, nullptr);
  }
  invalidate();
}

void setStatus(const std::string& text) {
  g_state.status = text;
  invalidate();
}

void openSection(int section) {
  if (section < SEC_CHATS || section > SEC_PROFILE) return;
  if (section != g_state.section) {
    g_state.section = section;
    g_scrollList = 0;
    g_hover = 0;
    layoutChildren();
  }
  if (g_cb.onSection) g_cb.onSection(section);
  invalidate();
}

} // namespace ui
} // namespace om
