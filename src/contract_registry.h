#pragma once

#include <windows.h>

namespace ContractRegistry
{
// Открыть фиксированный реестр в Excel и разместить окно слева.
// owner используется как родительское окно сообщений об ошибках.
void Open(HWND owner);
}
