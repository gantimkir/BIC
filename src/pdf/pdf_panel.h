#pragma once

#include <windows.h>

namespace PdfPanel
{
// Создаёт рамку PDF и область выбора как дочерние окна owner.
// content_left/content_width задают края области выбора, top/height — вертикальные
// границы рамки, side_overhang — выступ рамки с каждой стороны. Всё в пикселях
// клиентской области owner. При отказе false; дочерние окна удалит Windows вместе с owner.
bool Create(HWND owner, HINSTANCE instance, int content_left, int top,
    int content_width, int height, int side_overhang);
}
