#pragma once

#include <windows.h>

#include <atomic>
#include <string>

namespace PdfReportDialog
{
// Окно читает error/report только после done.load(acquire); рабочий поток
// публикует их через done.store(release). Все ссылки и дескриптор должны жить
// до возврата Show. Отмена запрашивается без ожидания завершения потока.
struct State
{
    const std::atomic_bool& done;
    std::atomic_bool& cancel;
    const DWORD& error;
    const std::wstring& report;
    HANDLE worker_thread;
};

// Показывает модальный отчёт с ожиданием данных; при закрытии запрашивает
// отмену незавершённой операции. Ошибку создания окна сообщает пользователю.
void Show(HWND owner, HINSTANCE instance, const State& state) noexcept;
}
