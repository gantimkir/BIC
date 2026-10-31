#pragma once
#include <windows.h>
namespace LandPlansEditor
{
// Модальный редактор работает с копией списка; только «Сохранить» публикует изменения.
void Show(HWND owner);
}
