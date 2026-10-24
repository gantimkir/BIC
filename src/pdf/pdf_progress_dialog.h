#pragma once

#include <windows.h>

#include <atomic>
#include <cstddef>

namespace PdfProgressDialog
{
// Состояние принадлежит вызывающему коду и живёт до возврата Show. Worker
// публикует processed отдельно, а error — через done(release/acquire).
// total фиксируется до запуска worker и не зависит от возможной очистки записей.
struct State
{
    const std::atomic_bool& done;
    const std::atomic_size_t& processed;
    std::atomic_bool& cancel;
    const DWORD& error;
    size_t total;
    HANDLE worker_thread;
};

// Показывает модальный прогресс и возвращает true только после успешной обработки
// всех PDF и показа каждого шага не менее 0,5 секунды. Отмена и сбой дают false;
// при отмене рабочего потока функция не ждёт его завершения.
bool Show(HWND owner, HINSTANCE instance, const State& state) noexcept;
}
