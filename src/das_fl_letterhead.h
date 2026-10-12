#pragma once

#include <windows.h>

namespace DasFlLetterhead
{
// Открывает заданный шаблон ФЛ в Excel и размещает окно в левой половине экрана.
// owner — владелец сообщения об ошибке; существующая открытая книга используется
// по правилам общего OfficeDocument::Open, новый документ из шаблона не создаётся.
void Open(HWND owner);
}
