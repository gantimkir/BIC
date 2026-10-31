#pragma once
#include <windows.h>
#include <string>

namespace ExplorerFolder
{
// Открывает абсолютный путь папки и асинхронно размещает соответствующее окно
// в bounds — непустой области экранных пикселей из рамки «Размещение».
// Копирует bounds в запрос; ошибки показывает владельцу.
void Open(HWND owner, const std::wstring& path, const RECT& bounds);
// Удаляет ожидающие операции и таймер перед уничтожением главного окна.
void Cancel();
}
