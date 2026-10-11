#pragma once

#include <windows.h>

namespace FileDropArea
{
// Создаёт область с OLE-приёмом файлов; nullptr означает ошибку создания или регистрации.
// Вызывать на UI-потоке STA с работающим циклом сообщений; OLE живёт до уничтожения окна.
// owner владеет дочерним окном и выступает владельцем модальных диалогов.
// Drop и выбор по щелчку принимают одну локальную папку и показывают размеры без изменения файлов.
HWND Create(HWND owner, HINSTANCE instance, int control_id,
    int left, int top, int width, int height);
}
