#pragma once

#include <windows.h>

namespace Pzz6148Text
{
// Открыть фиксированный текст ПЗЗ 61-48 в Word в левой половине рабочей области.
// owner используется как родительское окно сообщения об ошибке.
void Open(HWND owner);
}
