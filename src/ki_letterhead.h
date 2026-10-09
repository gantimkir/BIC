#pragma once

#include <windows.h>

namespace KiLetterhead
{
// Открыть фиксированный бланк КИ в Word в левой половине рабочей области.
// owner используется как родительское окно сообщения об ошибке.
void Open(HWND owner);
}
