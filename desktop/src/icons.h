// OrangeM Desktop - векторные иконки интерфейса.
// Аналог набора ICON_PATHS из веб-клиента (server/web/js/ui.js): те же фигуры 24x24,
// штрих 1.6 при размере 18, цвет передаётся вызывающим кодом (аналог currentColor).
#pragma once

#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#ifndef NOMINMAX
#define NOMINMAX
#endif

#include <windows.h>
// objidl.h обязателен ПЕРЕД gdiplus.h (иначе mingw не находит PROPID).
#include <objidl.h>
#include <gdiplus.h>
#include <string>

namespace om {
namespace ui {

// Рисует иконку name в квадрате (x, y, size, size). Неизвестное имя -> info.
void drawIcon(Gdiplus::Graphics& g, const std::string& name, int x, int y, int size,
              Gdiplus::Color color);

// true, если имя есть в наборе.
bool hasIcon(const std::string& name);

} // namespace ui
} // namespace om
