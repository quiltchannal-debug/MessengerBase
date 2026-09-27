// OrangeM Desktop - строгая тема: цвета и метрики (совпадают с веб-клиентом).
#pragma once
#include <windows.h>

namespace om {
namespace theme {

inline COLORREF rgb(unsigned r, unsigned g, unsigned b) { return RGB(r, g, b); }

// поверхности
const COLORREF bg        = RGB(0x0c, 0x0f, 0x13);
const COLORREF bg2       = RGB(0x11, 0x15, 0x1b);
const COLORREF panel     = RGB(0x15, 0x1a, 0x21);
const COLORREF panel2    = RGB(0x1a, 0x21, 0x2a);
const COLORREF line      = RGB(0x23, 0x2b, 0x35);
const COLORREF line2     = RGB(0x31, 0x3b, 0x47);
// текст
const COLORREF text      = RGB(0xe7, 0xec, 0xf2);
const COLORREF muted     = RGB(0x8d, 0x99, 0xa8);
const COLORREF muted2    = RGB(0x6b, 0x76, 0x81);
// акценты
const COLORREF orange    = RGB(0xf9, 0x73, 0x16);
const COLORREF orange2   = RGB(0xfb, 0x92, 0x3c);
const COLORREF orangeDim = RGB(0x2a, 0x1d, 0x12);
const COLORREF blue      = RGB(0x3b, 0x82, 0xf6);
const COLORREF blueDim   = RGB(0x14, 0x1e, 0x2f);
const COLORREF ok        = RGB(0x22, 0xc5, 0x5e);
const COLORREF danger    = RGB(0xef, 0x44, 0x44);
const COLORREF warn      = RGB(0xea, 0xb3, 0x08);
const COLORREF onOrange  = RGB(0x0c, 0x0f, 0x13);

// метрики
const int sidebarW   = 232;
const int listW      = 300;
const int headerH    = 56;
const int rowH       = 52;
const int statusH    = 26;
const int pad        = 16;
const int padS       = 8;
const int radius     = 8;

// шрифты (создаются один раз в ui.cpp)
HFONT fontUI();
HFONT fontBold();
HFONT fontSmall();
HFONT fontMono();
HFONT fontTitle();
void initFonts();

} // namespace theme
} // namespace om
