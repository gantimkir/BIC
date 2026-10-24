#pragma once

#include <windows.h>

namespace PdfPanel
{
// Создаёт рамку PDF и область выбора как дочерние окна owner.
// Координаты и размеры заданы в клиентской области owner. При отказе возвращает
// false; уже созданные дочерние окна удалит Windows при уничтожении owner.
bool Create(HWND owner, HINSTANCE instance, int left, int top, int width, int height);
}
