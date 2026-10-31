#pragma once

#include <windows.h>

namespace Pzz6148Graphics
{
// Открывает документ(ы) команды в области bounds, выбранной в рамке «Размещение».
// bounds — непустой прямоугольник в экранных пикселях; owner — владелец ошибок.
void Open(HWND owner, const RECT& bounds);
}
