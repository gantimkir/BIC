#include "file_drop_area.h"

#include "resource.h"

#include <ole2.h>
#include <shellapi.h>
#include <shobjidl.h>
#include <wrl/client.h>

#include <atomic>
#include <cwchar>
#include <memory>
#include <process.h>
#include <string_view>
#include <new>
#include <utility>
#include <string>
#include <vector>

namespace
{
constexpr wchar_t kAreaClass[] = L"BIC.FileDropArea";
constexpr COLORREF kAreaColor = RGB(215, 235, 250);
constexpr UINT kShowDroppedFiles = WM_APP + 1;

// Каждый запуск выбора файлов получает собственную ссылку на COM-квартиру потока.
// S_FALSE тоже требует CoUninitialize; при ошибке инициализации диалог не создаётся.
class ComApartment
{
public:
    ComApartment() : status_(CoInitializeEx(nullptr,
        COINIT_APARTMENTTHREADED | COINIT_DISABLE_OLE1DDE)) {}
    ~ComApartment()
    {
        if (SUCCEEDED(status_))
        {
            CoUninitialize();
        }
    }
    HRESULT status() const { return status_; }
    ComApartment(const ComApartment&) = delete;
    ComApartment& operator=(const ComApartment&) = delete;

private:
    HRESULT status_;
};

// Лимиты применяются до больших аллокаций. Место для префикса длинного пути
// зарезервировано отдельно; итоговый текст ограничен примерно 1 МиБ UTF-16.
constexpr size_t kMaxFiles = 1024;
constexpr size_t kMaxPathChars = 32760;
constexpr size_t kMaxTotalPathChars = 262144;
constexpr size_t kMaxReportChars = 524288;
constexpr SIZE_T kMaxDropBytes = 4 * 1024 * 1024;
constexpr UINT_PTR kMetadataTimer = 1;
constexpr ULONGLONG kMetadataTimeoutMs = 30000;
std::atomic<bool> metadata_worker_active{false};

struct SelectionRejected
{
    const wchar_t* message; // Только строковые литералы: описание ошибки не требует аллокации.
};

void ShowError(HWND owner, const wchar_t* action, unsigned long error) noexcept
{
    // Ошибку нехватки памяти нельзя обрабатывать через новую динамическую строку.
    wchar_t message[512]{};
    swprintf_s(message, L"%s\nКод ошибки: %lu", action, error);
    MessageBoxW(owner, message, L"PDF", MB_OK | MB_ICONERROR);
}

void CheckCount(size_t count)
{
    if (count > kMaxFiles)
    {
        throw SelectionRejected{L"За один раз можно выбрать не более 1024 файлов."};
    }
}

void CheckPath(std::wstring_view path, size_t& total_chars)
{
    if (path.empty() || path.size() > kMaxPathChars || path.find(L'\0') != std::wstring_view::npos)
    {
        throw SelectionRejected{L"Получен пустой или слишком длинный путь файла."};
    }
    if (path.size() > kMaxTotalPathChars - total_chars)
    {
        throw SelectionRejected{L"Суммарная длина путей превышает допустимый объём (262144 символа)."};
    }
    // Drop — внешние данные. Разрешены абсолютные файловые пути, но не устройства,
    // GLOBALROOT, относительные имена или специальные пространства Win32.
    const auto drive_path = [](std::wstring_view value)
    {
        return value.size() >= 3 &&
            ((value[0] >= L'A' && value[0] <= L'Z') || (value[0] >= L'a' && value[0] <= L'z')) &&
            value[1] == L':' && value[2] == L'\\';
    };
    const auto unc_path = [](std::wstring_view value)
    {
        const size_t slash = value.find(L'\\');
        return slash != std::wstring_view::npos && slash != 0 && slash + 1 < value.size() &&
            value[slash + 1] != L'\\';
    };
    bool accepted = drive_path(path);
    if (path.starts_with(L"\\\\?\\UNC\\"))
    {
        accepted = unc_path(path.substr(8));
    }
    else if (path.starts_with(L"\\\\?\\"))
    {
        accepted = drive_path(path.substr(4));
    }
    else if (path.starts_with(L"\\\\") && !path.starts_with(L"\\\\.\\"))
    {
        accepted = unc_path(path.substr(2));
    }
    if (!accepted)
    {
        throw SelectionRejected{L"Разрешены только абсолютные пути файлов на диске или в сетевой папке."};
    }
    total_chars += path.size();
}

class UniqueHandle
{
public:
    explicit UniqueHandle(HANDLE handle = nullptr) noexcept : handle_(handle) {}
    ~UniqueHandle()
    {
        if (handle_ != nullptr && handle_ != INVALID_HANDLE_VALUE)
        {
            CloseHandle(handle_);
        }
    }
    HANDLE get() const noexcept { return handle_; }
    void reset(HANDLE handle) noexcept
    {
        if (handle_ != nullptr && handle_ != INVALID_HANDLE_VALUE)
        {
            CloseHandle(handle_);
        }
        handle_ = handle;
    }
    UniqueHandle(const UniqueHandle&) = delete;
    UniqueHandle& operator=(const UniqueHandle&) = delete;

private:
    HANDLE handle_;
};

std::wstring MetadataPath(const std::wstring& path)
{
    // CheckPath уже отверг устройства. Расширенный префикс позволяет открыть длинный
    // путь без зависимости от longPathAware в манифесте приложения.
    if (path.starts_with(L"\\\\?\\"))
    {
        return path;
    }
    if (path.starts_with(L"\\\\"))
    {
        return L"\\\\?\\UNC\\" + path.substr(2);
    }
    return L"\\\\?\\" + path;
}

struct MetadataJob
{
    std::vector<std::wstring> paths;
    std::wstring report;
    DWORD error = ERROR_SUCCESS;
    std::atomic<bool> cancel{false};
    std::atomic<bool> done{false};
    // report/error пишет только worker. UI читает их лишь после done.load(acquire).
};

unsigned __stdcall MetadataWorker(void* parameter) noexcept
{
    // Поток владеет собственной ссылкой. После закрытия диалога он не использует HWND
    // или указатели на стек UI; задержавшийся драйвер не может обратиться к удалённому окну.
    std::unique_ptr<std::shared_ptr<MetadataJob>> argument(
        static_cast<std::shared_ptr<MetadataJob>*>(parameter));
    const std::shared_ptr<MetadataJob> job = *argument;
    argument.reset();
    try
    {
        for (const auto& path : job->paths)
        {
            if (job->cancel.load())
            {
                job->error = ERROR_CANCELLED;
                break;
            }
            std::wstring line = path + L": "; // Полный путь различает одинаковые имена.
            const std::wstring api_path = MetadataPath(path);
            // Без OPEN_REPARSE_POINT открывается целевой файл символьной ссылки.
            // Читаем только метаданные, не содержимое; совместный доступ не блокирует
            // редактирование/удаление другими программами. Дескриптор закрывается RAII.
            const UniqueHandle file(CreateFileW(api_path.c_str(), FILE_READ_ATTRIBUTES,
                FILE_SHARE_READ | FILE_SHARE_WRITE | FILE_SHARE_DELETE, nullptr,
                OPEN_EXISTING, FILE_FLAG_BACKUP_SEMANTICS, nullptr));
            if (job->cancel.load())
            {
                job->error = ERROR_CANCELLED;
                break;
            }
            DWORD file_error = ERROR_SUCCESS;
            if (file.get() == INVALID_HANDLE_VALUE)
            {
                file_error = GetLastError();
            }
            else if (GetFileType(file.get()) != FILE_TYPE_DISK)
            {
                file_error = ERROR_INVALID_DATA; // Устройства и каналы не являются файлами списка.
            }
            else
            {
                BY_HANDLE_FILE_INFORMATION info{};
                LARGE_INTEGER size{};
                if (!GetFileInformationByHandle(file.get(), &info))
                {
                    file_error = GetLastError();
                }
                else if ((info.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY) != 0)
                {
                    line += L"каталог, а не файл";
                }
                else if (!GetFileSizeEx(file.get(), &size))
                {
                    file_error = GetLastError();
                }
                else
                {
                    line += std::to_wstring(size.QuadPart) + L" байт";
                }
            }
            if (file_error != ERROR_SUCCESS)
            {
                line += L"размер недоступен (код " + std::to_wstring(file_error) + L")";
            }
            line += L"\r\n";
            if (line.size() > kMaxReportChars - job->report.size())
            {
                job->error = ERROR_BUFFER_OVERFLOW;
                break;
            }
            job->report += line;
        }
    }
    catch (const std::bad_alloc&)
    {
        job->error = ERROR_NOT_ENOUGH_MEMORY;
        job->report.clear();
    }
    catch (...)
    {
        job->error = ERROR_INVALID_DATA;
        job->report.clear();
    }
    job->done.store(true, std::memory_order_release);
    metadata_worker_active.store(false);
    return 0;
}

struct MetadataDialogContext
{
    std::shared_ptr<MetadataJob> job;
    UniqueHandle thread;
    ULONGLONG started = GetTickCount64();
};

void CancelMetadata(MetadataDialogContext* context) noexcept
{
    if (context != nullptr && !context->job->done.load(std::memory_order_acquire))
    {
        context->job->cancel.store(true);
        // API лишь запрашивает отмену и не ждёт окончания. Если драйвер её задержит,
        // worker сохраняет свою ссылку на job; новый worker не запускается до завершения.
        CancelSynchronousIo(context->thread.get());
    }
}

INT_PTR CALLBACK SizesDialogProcedure(HWND dialog, UINT message, WPARAM w_param, LPARAM l_param) noexcept
{
    auto* context = reinterpret_cast<MetadataDialogContext*>(GetWindowLongPtrW(dialog, DWLP_USER));
    if (message == WM_INITDIALOG)
    {
        context = reinterpret_cast<MetadataDialogContext*>(l_param);
        SetWindowLongPtrW(dialog, DWLP_USER, reinterpret_cast<LONG_PTR>(context));
        SetDlgItemTextW(dialog, IDC_FILE_SIZES_TEXT, L"Получение размеров файлов…");
        EnableWindow(GetDlgItem(dialog, IDOK), FALSE);
        if (SetTimer(dialog, kMetadataTimer, 100, nullptr) == 0)
        {
            const DWORD error = GetLastError();
            CancelMetadata(context);
            ShowError(dialog, L"Не удалось включить ожидание результата.", error);
            EndDialog(dialog, IDCANCEL);
            return TRUE;
        }
        SetFocus(GetDlgItem(dialog, IDCANCEL));
        return FALSE;
    }
    if (message == WM_TIMER && w_param == kMetadataTimer && context != nullptr)
    {
        const bool done = context->job->done.load(std::memory_order_acquire);
        if (done || GetTickCount64() - context->started >= kMetadataTimeoutMs)
        {
            KillTimer(dialog, kMetadataTimer);
            const wchar_t* text = nullptr;
            if (!done)
            {
                CancelMetadata(context);
                text = L"Время ожидания превысило 30 секунд. Операция отменена.";
            }
            else if (context->job->error == ERROR_NOT_ENOUGH_MEMORY)
            {
                text = L"Недостаточно памяти для списка файлов. Выберите меньше файлов.";
            }
            else if (context->job->error != ERROR_SUCCESS)
            {
                text = L"Получение размеров отменено или список превышает допустимый объём.";
            }
            else
            {
                text = context->job->report.c_str();
            }
            if (!SetDlgItemTextW(dialog, IDC_FILE_SIZES_TEXT, text))
            {
                ShowError(dialog, L"Не удалось показать список файлов.", GetLastError());
                EndDialog(dialog, IDCANCEL);
                return TRUE;
            }
            EnableWindow(GetDlgItem(dialog, IDOK), TRUE);
            ShowWindow(GetDlgItem(dialog, IDCANCEL), SW_HIDE);
            SetFocus(GetDlgItem(dialog, IDOK));
        }
        return TRUE;
    }
    if ((message == WM_COMMAND && (LOWORD(w_param) == IDOK || LOWORD(w_param) == IDCANCEL)) ||
        message == WM_CLOSE)
    {
        CancelMetadata(context);
        EndDialog(dialog, message == WM_CLOSE ? IDCANCEL : LOWORD(w_param));
        return TRUE;
    }
    if (message == WM_DESTROY)
    {
        KillTimer(dialog, kMetadataTimer);
        CancelMetadata(context);
        SetWindowLongPtrW(dialog, DWLP_USER, 0);
        return TRUE;
    }
    return FALSE;
}

void ShowFileSizes(HWND owner, std::vector<std::wstring> paths)
{
    if (paths.empty())
    {
        return;
    }
    CheckCount(paths.size());
    size_t total_chars = 0;
    for (const auto& path : paths)
    {
        CheckPath(path, total_chars);
    }
    bool expected = false;
    if (!metadata_worker_active.compare_exchange_strong(expected, true))
    {
        MessageBoxW(owner, L"Предыдущая операция ещё завершается. Повторите выбор позже.",
            L"PDF", MB_OK | MB_ICONINFORMATION);
        return;
    }
    struct ActiveSlot
    {
        bool worker_started = false;
        ~ActiveSlot()
        {
            if (!worker_started)
            {
                metadata_worker_active.store(false);
            }
        }
    } slot;
    const auto job = std::make_shared<MetadataJob>();
    job->paths = std::move(paths);
    auto argument = std::make_unique<std::shared_ptr<MetadataJob>>(job);
    MetadataDialogContext context{job};
    const uintptr_t thread = _beginthreadex(nullptr, 0, MetadataWorker, argument.get(), 0, nullptr);
    if (thread == 0)
    {
        ShowError(owner, L"Не удалось запустить получение размеров.", ERROR_NOT_ENOUGH_MEMORY);
        return;
    }
    argument.release(); // С этого момента памятью аргумента владеет worker.
    context.thread.reset(reinterpret_cast<HANDLE>(thread));
    slot.worker_started = true;
    // Модальный цикл обслуживает сообщения, а UI не ждёт ни метаданных, ни thread join.
    // После отмены/ошибки диалога закрываем только HANDLE, не завершаем поток принудительно.
    const auto instance = reinterpret_cast<HINSTANCE>(GetWindowLongPtrW(owner, GWLP_HINSTANCE));
    const INT_PTR result = DialogBoxParamW(instance, MAKEINTRESOURCEW(IDD_FILE_SIZES), owner,
        SizesDialogProcedure, reinterpret_cast<LPARAM>(&context));
    const DWORD dialog_error = result == -1 ? GetLastError() : ERROR_SUCCESS;
    CancelMetadata(&context);
    if (result == -1)
    {
        ShowError(owner, L"Не удалось показать размеры файлов.", dialog_error);
    }
}

void ChooseFiles(HWND owner)
{
    const ComApartment apartment;
    if (FAILED(apartment.status()))
    {
        ShowError(owner, L"Не удалось инициализировать выбор файлов.",
            static_cast<unsigned long>(apartment.status()));
        return;
    }
    // Системный IFileOpenDialog поддерживает несколько файлов без фиксированного
    // буфера путей; ограничение старого OPENFILENAME на длину списка здесь отсутствует.
    Microsoft::WRL::ComPtr<IFileOpenDialog> dialog;
    HRESULT result = CoCreateInstance(CLSID_FileOpenDialog, nullptr, CLSCTX_INPROC_SERVER,
        IID_PPV_ARGS(dialog.GetAddressOf()));
    FILEOPENDIALOGOPTIONS options{};
    if (SUCCEEDED(result))
    {
        result = dialog->GetOptions(&options);
    }
    if (SUCCEEDED(result))
    {
        result = dialog->SetOptions(options | FOS_ALLOWMULTISELECT | FOS_FORCEFILESYSTEM |
            FOS_FILEMUSTEXIST | FOS_PATHMUSTEXIST | FOS_NOCHANGEDIR);
    }
    // Фильтр системного диалога показывает PDF; множественный выбор сохранён.
    const COMDLG_FILTERSPEC filter[] = {{L"PDF-файлы (*.pdf)", L"*.pdf"}};
    if (SUCCEEDED(result))
    {
        result = dialog->SetFileTypes(1, filter);
    }
    if (SUCCEEDED(result))
    {
        result = dialog->SetTitle(L"Выбор PDF-файлов");
    }
    if (SUCCEEDED(result))
    {
        result = dialog->Show(owner);
    }
    if (result == HRESULT_FROM_WIN32(ERROR_CANCELLED))
    {
        return; // Отмена не показывает ошибок и не меняет состояние формы.
    }

    Microsoft::WRL::ComPtr<IShellItemArray> items;
    DWORD count = 0;
    if (SUCCEEDED(result))
    {
        result = dialog->GetResults(items.GetAddressOf());
    }
    if (SUCCEEDED(result))
    {
        result = items->GetCount(&count);
    }
    std::vector<std::wstring> paths;
    CheckCount(count);
    size_t total_chars = 0;
    for (DWORD index = 0; SUCCEEDED(result) && index < count; ++index)
    {
        Microsoft::WRL::ComPtr<IShellItem> item;
        result = items->GetItemAt(index, item.GetAddressOf());
        // Даже если копирование пути выбросит исключение, память Shell будет освобождена.
        struct TaskMemString
        {
            PWSTR value = nullptr;
            ~TaskMemString() { CoTaskMemFree(value); }
        } path;
        if (SUCCEEDED(result))
        {
            result = item->GetDisplayName(SIGDN_FILESYSPATH, &path.value);
        }
        if (SUCCEEDED(result))
        {
            if (path.value == nullptr)
            {
                result = E_UNEXPECTED;
                break;
            }
            const size_t length = wcsnlen_s(path.value, kMaxPathChars + 1);
            const std::wstring_view view(path.value, length);
            CheckPath(view, total_chars);
            paths.emplace_back(view);
        }
    }
    if (FAILED(result))
    {
        ShowError(owner, L"Не удалось выбрать файлы.", static_cast<unsigned long>(result));
        return;
    }
    ShowFileSizes(owner, std::move(paths));
}

// Проверяем границы DROPFILES и двойной терминатор до вызовов Shell над внешним
// HGLOBAL. Ограничение сырого блока не позволяет сканировать произвольно большой вход.
void ValidateDropPayload(HDROP drop)
{
    const auto memory = reinterpret_cast<HGLOBAL>(drop);
    const SIZE_T bytes = GlobalSize(memory);
    if (bytes < sizeof(DROPFILES) || bytes > kMaxDropBytes)
    {
        throw SelectionRejected{L"Повреждён или слишком велик список перетаскиваемых файлов (максимум 4 МиБ)."};
    }
    const auto* data = static_cast<const BYTE*>(GlobalLock(memory));
    if (data == nullptr)
    {
        throw SelectionRejected{L"Не удалось прочитать список перетаскиваемых файлов."};
    }
    struct UnlockGuard
    {
        HGLOBAL memory;
        ~UnlockGuard() { GlobalUnlock(memory); }
    } guard{memory};
    const auto* header = reinterpret_cast<const DROPFILES*>(data);
    const SIZE_T offset = header->pFiles;
    if (offset < sizeof(DROPFILES) || offset > bytes - 2)
    {
        throw SelectionRejected{L"Повреждены границы списка перетаскиваемых файлов."};
    }
    bool terminated = false;
    if (header->fWide)
    {
        if (offset % sizeof(wchar_t) != 0)
        {
            throw SelectionRejected{L"Повреждён Unicode-список перетаскиваемых файлов."};
        }
        const auto* names = reinterpret_cast<const wchar_t*>(data + offset);
        const SIZE_T count = (bytes - offset) / sizeof(wchar_t);
        for (SIZE_T index = 1; index < count; ++index)
        {
            if (names[index - 1] == L'\0' && names[index] == L'\0')
            {
                terminated = true;
                break;
            }
        }
    }
    else
    {
        for (SIZE_T index = offset + 1; index < bytes; ++index)
        {
            if (data[index - 1] == 0 && data[index] == 0)
            {
                terminated = true;
                break;
            }
        }
    }
    if (!terminated)
    {
        throw SelectionRejected{L"Список перетаскиваемых файлов не имеет корректного окончания."};
    }
}

// Только заимствуем CF_HDROP: WM_DROPFILES освобождается DragFinish,
// а IDataObject::GetData — ReleaseStgMedium. Лимиты проверяются до аллокаций.
std::vector<std::wstring> ReadDropPaths(HDROP drop)
{
    ValidateDropPayload(drop);
    const UINT count = DragQueryFileW(drop, 0xFFFFFFFF, nullptr, 0);
    CheckCount(count);
    std::vector<std::wstring> paths;
    paths.reserve(count);
    size_t total_chars = 0;
    for (UINT index = 0; index < count; ++index)
    {
        const UINT length = DragQueryFileW(drop, index, nullptr, 0);
        if (length == 0 || length > kMaxPathChars || length > kMaxTotalPathChars - total_chars)
        {
            throw SelectionRejected{L"Список содержит пустой путь или превышает допустимую длину."};
        }
        std::wstring path(static_cast<size_t>(length) + 1, L'\0');
        const UINT copied = DragQueryFileW(drop, index, path.data(), length + 1);
        if (copied != length)
        {
            throw SelectionRejected{L"Не удалось полностью получить путь перетаскиваемого файла."};
        }
        path.resize(copied);
        CheckPath(path, total_chars);
        paths.push_back(std::move(path));
    }
    return paths;
}

std::vector<std::wstring> DroppedPaths(HDROP drop)
{
    struct DropGuard
    {
        HDROP handle;
        ~DropGuard() { DragFinish(handle); }
    } guard{drop};
    return ReadDropPaths(drop); // Резервный протокол Shell сохраняет свой порядок освобождения.
}

// Современный Проводник согласует перенос через OLE. Цель принимает только
// файловый CF_HDROP и эффект COPY: источник никогда не должен удалять файлы.
// Окно владеет одной ссылкой, RegisterDragDrop добавляет ещё одну; при уничтожении
// сначала выполняется RevokeDragDrop, затем освобождается ссылка окна.
class FileDropTarget final : public IDropTarget
{
public:
    explicit FileDropTarget(HWND window) : window_(window) {}

    HRESULT STDMETHODCALLTYPE QueryInterface(REFIID iid, void** object) override
    {
        if (object == nullptr)
        {
            return E_POINTER;
        }
        *object = nullptr;
        if (IsEqualIID(iid, IID_IUnknown) || IsEqualIID(iid, IID_IDropTarget))
        {
            *object = static_cast<IDropTarget*>(this);
            AddRef();
            return S_OK;
        }
        return E_NOINTERFACE;
    }
    ULONG STDMETHODCALLTYPE AddRef() override
    {
        return static_cast<ULONG>(InterlockedIncrement(&references_));
    }
    ULONG STDMETHODCALLTYPE Release() override
    {
        const LONG count = InterlockedDecrement(&references_);
        if (count == 0)
        {
            delete this;
        }
        return static_cast<ULONG>(count);
    }
    HRESULT STDMETHODCALLTYPE DragEnter(IDataObject* data, DWORD, POINTL, DWORD* effect) override
    {
        FORMATETC format{CF_HDROP, nullptr, DVASPECT_CONTENT, -1, TYMED_HGLOBAL};
        accepts_files_ = data != nullptr && SUCCEEDED(data->QueryGetData(&format));
        return DragOver(0, {}, effect);
    }
    HRESULT STDMETHODCALLTYPE DragOver(DWORD, POINTL, DWORD* effect) override
    {
        if (effect == nullptr)
        {
            return E_POINTER;
        }
        // Модальное окно отключает владельца: новые drop во время показа не принимаем.
        *effect = accepts_files_ && IsWindowEnabled(GetParent(window_)) &&
            (*effect & DROPEFFECT_COPY) != 0 && pending_paths_.empty() && pending_error_ == nullptr
            ? DROPEFFECT_COPY : DROPEFFECT_NONE;
        return S_OK;
    }
    HRESULT STDMETHODCALLTYPE DragLeave() override
    {
        accepts_files_ = false;
        return S_OK;
    }
    HRESULT STDMETHODCALLTYPE Drop(IDataObject* data, DWORD keys, POINTL point, DWORD* effect) noexcept override
    {
        try
        {
            return DropImpl(data, keys, point, effect);
        }
        catch (const SelectionRejected& error)
        {
            if (effect != nullptr) { *effect = DROPEFFECT_NONE; }
            return QueueFailure(error.message, E_INVALIDARG);
        }
        catch (const std::bad_alloc&)
        {
            if (effect != nullptr) { *effect = DROPEFFECT_NONE; }
            return QueueFailure(L"Недостаточно памяти для списка файлов. Выберите меньше файлов.", E_OUTOFMEMORY);
        }
        catch (...)
        {
            if (effect != nullptr) { *effect = DROPEFFECT_NONE; }
            return QueueFailure(L"Не удалось обработать перетаскиваемые файлы.", E_UNEXPECTED);
        }
    }
    HRESULT DropImpl(IDataObject* data, DWORD, POINTL, DWORD* effect)
    {
        if (effect == nullptr)
        {
            return E_POINTER;
        }
        const bool copy_allowed = (*effect & DROPEFFECT_COPY) != 0;
        *effect = DROPEFFECT_NONE;
        accepts_files_ = false;
        if (data == nullptr || !copy_allowed || !IsWindowEnabled(GetParent(window_)) ||
            !pending_paths_.empty() || pending_error_ != nullptr)
        {
            return S_OK;
        }
        FORMATETC format{CF_HDROP, nullptr, DVASPECT_CONTENT, -1, TYMED_HGLOBAL};
        struct MediumGuard
        {
            STGMEDIUM medium{};
            bool acquired = false;
            ~MediumGuard()
            {
                if (acquired)
                {
                    ReleaseStgMedium(&medium);
                }
            }
        } storage;
        const HRESULT result = data->GetData(&format, &storage.medium);
        if (FAILED(result))
        {
            return result;
        }
        storage.acquired = true;
        if (storage.medium.tymed != TYMED_HGLOBAL || storage.medium.hGlobal == nullptr)
        {
            return DV_E_TYMED;
        }
        pending_paths_ = ReadDropPaths(reinterpret_cast<HDROP>(storage.medium.hGlobal));
        // Освобождаем данные источника до постановки сообщения с модальным диалогом.
        ReleaseStgMedium(&storage.medium);
        storage.acquired = false;
        if (pending_paths_.empty())
        {
            return S_OK;
        }
        // Завершаем Drop до модального диалога, чтобы Проводник вышел из своего
        // цикла переноса. Список принадлежит объекту окна, не сырому указателю сообщения.
        if (!PostMessageW(window_, kShowDroppedFiles, 0, 0))
        {
            const DWORD error = GetLastError();
            pending_paths_.clear();
            return HRESULT_FROM_WIN32(error);
        }
        *effect = DROPEFFECT_COPY;
        return S_OK;
    }
    std::vector<std::wstring> TakePaths()
    {
        // Перемещаем список перед модальным циклом, чтобы освободить состояние drop.
        return std::exchange(pending_paths_, {});
    }

    const wchar_t* TakeError() noexcept
    {
        return std::exchange(pending_error_, nullptr);
    }

private:
    // Ошибка ставится в очередь без выделения памяти. При уничтожении окна
    // ни текст, ни список не оставляют висячих указателей в очереди сообщений.
    HRESULT QueueFailure(const wchar_t* message, HRESULT result) noexcept
    {
        pending_paths_.clear();
        pending_error_ = message;
        if (!PostMessageW(window_, kShowDroppedFiles, 0, 0))
        {
            pending_error_ = nullptr;
        }
        return result;
    }
    ~FileDropTarget() = default;
    volatile LONG references_ = 1;
    HWND window_;
    bool accepts_files_ = false;
    const wchar_t* pending_error_ = nullptr;
    std::vector<std::wstring> pending_paths_;
};

LRESULT AreaWindowProcedureImpl(HWND window, UINT message, WPARAM w_param, LPARAM l_param)
{
    switch (message)
    {
    case WM_CREATE:
    {
        // RegisterDragDrop требует OleInitialize, одного CoInitializeEx недостаточно.
        // Инициализация живёт весь срок окна; S_FALSE тоже балансируется OleUninitialize.
        const HRESULT ole_result = OleInitialize(nullptr);
        if (FAILED(ole_result))
        {
            ShowError(GetParent(window), L"Не удалось включить перетаскивание файлов.",
                static_cast<unsigned long>(ole_result));
            return -1;
        }
        auto* target = new (std::nothrow) FileDropTarget(window);
        const HRESULT register_result = target != nullptr ? RegisterDragDrop(window, target) : E_OUTOFMEMORY;
        if (FAILED(register_result))
        {
            if (target != nullptr)
            {
                target->Release();
            }
            OleUninitialize();
            ShowError(GetParent(window), L"Не удалось зарегистрировать область файлов.",
                static_cast<unsigned long>(register_result));
            return -1;
        }
        SetWindowLongPtrW(window, GWLP_USERDATA, reinterpret_cast<LONG_PTR>(target));
        DragAcceptFiles(window, TRUE); // Резервный WM_DROPFILES для старых источников.
        return 0;
    }
    case WM_DESTROY:
    {
        DragAcceptFiles(window, FALSE);
        auto* target = reinterpret_cast<FileDropTarget*>(GetWindowLongPtrW(window, GWLP_USERDATA));
        if (target != nullptr)
        {
            RevokeDragDrop(window);
            SetWindowLongPtrW(window, GWLP_USERDATA, 0);
            target->Release();
            OleUninitialize();
        }
        return 0;
    }
    case kShowDroppedFiles:
    {
        auto* target = reinterpret_cast<FileDropTarget*>(GetWindowLongPtrW(window, GWLP_USERDATA));
        if (target != nullptr)
        {
            if (const wchar_t* error = target->TakeError())
            {
                MessageBoxW(GetParent(window), error, L"PDF", MB_OK | MB_ICONERROR);
            }
            else
            {
                ShowFileSizes(GetParent(window), target->TakePaths());
            }
        }
        return 0;
    }
    case WM_DROPFILES:
        if (!IsWindowEnabled(GetParent(window)))
        {
            DragFinish(reinterpret_cast<HDROP>(w_param));
            return 0;
        }
        ShowFileSizes(GetParent(window), DroppedPaths(reinterpret_cast<HDROP>(w_param)));
        return 0;
    case WM_LBUTTONDOWN:
        SetFocus(window);
        SetCapture(window);
        return 0;
    case WM_LBUTTONUP:
        if (GetCapture() == window)
        {
            ReleaseCapture();
            RECT client{};
            GetClientRect(window, &client);
            // При отпускании за пределами области щелчок отменяется.
            const POINT point{static_cast<short>(LOWORD(l_param)), static_cast<short>(HIWORD(l_param))};
            if (PtInRect(&client, point))
            {
                ChooseFiles(GetParent(window));
            }
        }
        return 0;
    case WM_KEYDOWN:
        if ((w_param == VK_RETURN || w_param == VK_SPACE) && (l_param & (1LL << 30)) == 0)
        {
            ChooseFiles(GetParent(window));
            return 0;
        }
        break;
    case WM_SETFOCUS:
    case WM_KILLFOCUS:
        InvalidateRect(window, nullptr, FALSE);
        return 0;
    case WM_ERASEBKGND:
        return 1; // WM_PAINT полностью заполняет фон, чтобы не было мерцания.
    case WM_PAINT:
    {
        PAINTSTRUCT paint{};
        HDC dc = BeginPaint(window, &paint);
        RECT client{};
        GetClientRect(window, &client);
        // DC_BRUSH принадлежит Windows; цвет задаётся только для этого контекста рисования.
        const auto brush = static_cast<HBRUSH>(GetStockObject(DC_BRUSH));
        SetDCBrushColor(dc, kAreaColor);
        FillRect(dc, &client, brush);
        FrameRect(dc, &client, static_cast<HBRUSH>(GetStockObject(GRAY_BRUSH)));
        const HGDIOBJ old_font = SelectObject(dc, GetStockObject(DEFAULT_GUI_FONT));
        SetBkMode(dc, TRANSPARENT);
        SetTextColor(dc, RGB(25, 60, 90));
        DrawTextW(dc, L"Выбрать файлы", -1, &client, DT_CENTER | DT_VCENTER | DT_SINGLELINE);
        if (GetFocus() == window)
        {
            InflateRect(&client, -3, -3);
            DrawFocusRect(dc, &client);
        }
        SelectObject(dc, old_font);
        EndPaint(window, &paint);
        return 0;
    }
    }
    return DefWindowProcW(window, message, w_param, l_param);
}
// Ни одно исключение C++ из выбора, разбора drop или запуска worker не выходит
// за границу WNDPROC. В обработчике ошибки используем только статический текст.
LRESULT CALLBACK AreaWindowProcedure(HWND window, UINT message, WPARAM w_param, LPARAM l_param) noexcept
{
    try
    {
        return AreaWindowProcedureImpl(window, message, w_param, l_param);
    }
    catch (const SelectionRejected& error)
    {
        MessageBoxW(GetParent(window), error.message, L"PDF", MB_OK | MB_ICONERROR);
    }
    catch (const std::bad_alloc&)
    {
        MessageBoxW(GetParent(window), L"Недостаточно памяти. Выберите меньше файлов.",
            L"PDF", MB_OK | MB_ICONERROR);
    }
    catch (...)
    {
        MessageBoxW(GetParent(window), L"Не удалось обработать файлы.", L"PDF", MB_OK | MB_ICONERROR);
    }
    return message == WM_CREATE ? -1 : 0;
}
} // namespace

HWND FileDropArea::Create(HWND owner, HINSTANCE instance, int control_id,
    int left, int top, int width, int height)
{
    const WNDCLASSEXW area_class{
        .cbSize = sizeof(WNDCLASSEXW),
        .style = CS_HREDRAW | CS_VREDRAW,
        .lpfnWndProc = AreaWindowProcedure,
        .hInstance = instance,
        .hCursor = LoadCursorW(nullptr, IDC_HAND),
        .lpszClassName = kAreaClass,
    };
    if (RegisterClassExW(&area_class) == 0 && GetLastError() != ERROR_CLASS_ALREADY_EXISTS)
    {
        return nullptr;
    }
    HWND area = CreateWindowExW(0, kAreaClass, L"Выбрать файлы", WS_CHILD | WS_VISIBLE | WS_TABSTOP,
        left, top, width, height, owner,
        reinterpret_cast<HMENU>(static_cast<INT_PTR>(control_id)), instance, nullptr);
    if (area != nullptr)
    {
        // Рамка и область — соседние окна. Область должна быть выше GroupBox
        // в Z-порядке, иначе рамка может оказаться целью наведения вместо неё.
        SetWindowPos(area, HWND_TOP, 0, 0, 0, 0, SWP_NOMOVE | SWP_NOSIZE | SWP_NOACTIVATE);
    }
    return area;
}
