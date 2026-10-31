#pragma once

#include <windows.h>
#include "explorer_folder.h"

namespace LandPlansMenu
{
// Перечитывает активный список рядом с EXE (user имеет приоритет) и показывает меню.
// Выбор открывает папку в выбранной области; ошибки показывает владельцу.
void Show(HWND owner, HWND button, const RECT& opening_bounds);
// Обслуживают owner-draw заголовки только активного меню, чужие элементы пропускают.
bool MeasureSeparator(MEASUREITEMSTRUCT& item);
bool DrawMenuSeparator(const DRAWITEMSTRUCT& item);
// Общая отрисовка разделителя для меню и предварительного просмотра редактора.
void DrawSeparator(HDC dc, RECT bounds, const std::wstring& text, HFONT font);
}
