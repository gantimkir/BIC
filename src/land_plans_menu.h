#pragma once

#include <windows.h>
#include <optional>
#include <string>

namespace LandPlansMenu
{
// Перечитывает активный список рядом с EXE (user имеет приоритет) и показывает меню.
// Возвращает путь выбранной папки после закрытия меню; при отмене или ошибке
// возвращает nullopt. Ошибки показывает владельцу. Проводник не запускает.
std::optional<std::wstring> Select(HWND owner, HWND button);
// Обслуживают owner-draw заголовки только активного меню, чужие элементы пропускают.
bool MeasureSeparator(MEASUREITEMSTRUCT& item);
bool DrawMenuSeparator(const DRAWITEMSTRUCT& item);
// Общая отрисовка разделителя для меню и предварительного просмотра редактора.
void DrawSeparator(HDC dc, RECT bounds, const std::wstring& text, HFONT font);
}
