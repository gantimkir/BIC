#pragma once

#include <windows.h>

namespace IpLetterhead
{
// Открыть фиксированный бланк в Word слева от BIC.
// owner используется как родительское окно сообщения об ошибке.
void Open(HWND owner);
}
