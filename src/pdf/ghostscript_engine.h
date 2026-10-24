#pragma once

#include <windows.h>
#include <atomic>
#include <string>
#include <vector>

namespace Ghostscript
{
enum class Stage { Validate, Load, Exports, Revision, Create, Configure, Execute, Exit, Complete };

struct Result
{
    HRESULT status = E_FAIL;
    Stage stage = Stage::Validate;
    // Сырые коды API: Quit (-101) и Info (-110) могут означать штатное завершение.
    int api_code = 0;
    int exit_code = 0;
    long revision = 0;
    long revision_date = 0;
    std::string product;
    std::wstring dll_path; // Фактически выбранная DLL, в том числе при ошибке загрузки.
    // Байты stdout/stderr без предположения об их кодировке; общий лимит 64 КиБ.
    std::string diagnostics;
    bool diagnostics_truncated = false;
    bool cancelled = false;
    explicit operator bool() const noexcept { return SUCCEEDED(status); }
};

struct RunOptions
{
    // По умолчанию — runtime/ghostscript/bin/gsdll64.dll рядом с BIC.exe.
    // Непустое значение — полный путь доверенной DLL для разработки.
    std::wstring dll_path;
    // Отдельные аргументы без argv[0] и без внешних кавычек. Это низкоуровневый
    // доверенный ввод: параметры могут создавать/перезаписывать файлы.
    // BATCH/NOPAUSE/SAFER добавляются перед ними; профили сжатия здесь не задаются.
    std::vector<std::wstring> arguments;
    // Необязательный флаг живёт до возврата Run. Отмена кооперативная;
    // работоспособность polling зависит от CHECK_INTERRUPTS в поставке DLL.
    const std::atomic_bool* cancel = nullptr;
};

// Загружает DLL, проверяет необходимые экспорты и получает версию, без обработки PDF.
// Пустой путь выбирает комплект рядом с EXE, независимо от текущего каталога.
// Отсутствие DLL возвращается как ошибка; поиск системной установки не выполняется.
Result Inspect(const std::wstring& dll_path = {});

// Синхронная пакетная операция: вызывать из рабочего потока. Вся жизнь экземпляра
// и все gsapi-вызовы проходят в вызывающем потоке. Параллельный вход через этот
// модуль возвращает ERROR_BUSY. DLL выгружается лишь после завершения экземпляра.
// Успех API не подтверждает качество/размер PDF; файлы и временные результаты
// контролирует вызывающий модуль. Нельзя принудительно завершать рабочий поток.
Result Run(const RunOptions& options);
}
