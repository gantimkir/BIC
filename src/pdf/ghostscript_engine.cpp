#include "ghostscript_engine.h"

#include <climits>
#include <filesystem>
#include <mutex>
#include <new>
#include <utility>

namespace
{
// Узкая декларация публичного C ABI, а не исходники движка. Сигнатуры соответствуют
// psi/iapi.h; значения кодов — base/gserrors.h официального Ghostscript.
// https://ghostscript.readthedocs.io/en/latest/API.html
// SDK/импортная LIB не нужны: адреса разрешаются при загрузке DLL.
struct RevisionInfo
{
    const char* product;
    const char* copyright;
    long revision;
    long revision_date;
};
using Input = int (__stdcall*)(void*, char*, int);
using Output = int (__stdcall*)(void*, const char*, int);
using Poll = int (__stdcall*)(void*);
using Revision = int (__stdcall*)(RevisionInfo*, int);
using NewInstance = int (__stdcall*)(void**, void*);
using DeleteInstance = void (__stdcall*)(void*);
using SetStdio = int (__stdcall*)(void*, Input, Output, Output);
using SetPoll = int (__stdcall*)(void*, Poll);
using SetEncoding = int (__stdcall*)(void*, int);
using Initialize = int (__stdcall*)(void*, int, char**);
using Exit = int (__stdcall*)(void*);
constexpr int kUtf8 = 1;
constexpr int kQuit = -101;
constexpr int kInfo = -110;
constexpr int kInterrupt = -6;
constexpr size_t kLogLimit = 64 * 1024;
// Сериализуем даже разные DLL/проверку версии: независимые экземпляры движка
// не считаются автоматически безопасными для одновременного использования.
std::mutex engine_mutex;

struct Library
{
    HMODULE module = nullptr;
    Revision revision = nullptr;
    NewInstance create = nullptr;
    DeleteInstance destroy = nullptr;
    SetStdio stdio = nullptr;
    SetPoll poll = nullptr;
    SetEncoding encoding = nullptr;
    Initialize initialize = nullptr;
    Exit exit = nullptr;
    Library() = default;
    Library(const Library&) = delete;
    Library& operator=(const Library&) = delete;
    ~Library() { if (module != nullptr) FreeLibrary(module); }
};

template<class T>
bool Resolve(HMODULE module, const char* name, T& function)
{
    // Преобразование FARPROC — штатная граница динамического Win32 API;
    // тип и соглашение вызова должны совпадать с экспортом Ghostscript.
    function = reinterpret_cast<T>(GetProcAddress(module, name));
    return function != nullptr;
}

bool Load(const std::wstring& path, Library& library, Ghostscript::Result& result)
{
    if (path.empty() || path.find(L'\0') != std::wstring::npos ||
        !std::filesystem::path(path).is_absolute())
    {
        result.status = E_INVALIDARG;
        return false;
    }
    result.stage = Ghostscript::Stage::Load;
    // Не ищем DLL/зависимости в текущей папке и PATH. Ресурсы Ghostscript
    // поставляются вместе с DLL; системная установка движка не нужна.
    library.module = LoadLibraryExW(path.c_str(), nullptr,
        LOAD_LIBRARY_SEARCH_DLL_LOAD_DIR | LOAD_LIBRARY_SEARCH_SYSTEM32);
    if (library.module == nullptr)
    {
        result.status = HRESULT_FROM_WIN32(GetLastError());
        return false;
    }
    result.stage = Ghostscript::Stage::Exports;
    if (!Resolve(library.module, "gsapi_revision", library.revision) ||
        !Resolve(library.module, "gsapi_new_instance", library.create) ||
        !Resolve(library.module, "gsapi_delete_instance", library.destroy) ||
        !Resolve(library.module, "gsapi_set_stdio", library.stdio) ||
        !Resolve(library.module, "gsapi_set_poll", library.poll) ||
        !Resolve(library.module, "gsapi_set_arg_encoding", library.encoding) ||
        !Resolve(library.module, "gsapi_init_with_args", library.initialize) ||
        !Resolve(library.module, "gsapi_exit", library.exit))
    {
        result.status = HRESULT_FROM_WIN32(ERROR_PROC_NOT_FOUND);
        return false;
    }
    result.stage = Ghostscript::Stage::Revision;
    RevisionInfo info{};
    result.api_code = library.revision(&info, static_cast<int>(sizeof(info)));
    if (result.api_code != 0)
    {
        result.status = HRESULT_FROM_WIN32(ERROR_REVISION_MISMATCH);
        return false;
    }
    // Строки DLL копируем до выгрузки, чтобы результат не содержал висячих указателей.
    result.product = info.product != nullptr ? info.product : "";
    result.revision = info.revision;
    result.revision_date = info.revision_date;
    return true;
}

struct Context
{
    Ghostscript::Result& result;
    const std::atomic_bool* cancel;
};

int __stdcall ReadInput(void*, char*, int) noexcept
{
    // Пакетная операция никогда не ждёт консольный ввод (в том числе пароль).
    return 0;
}

int __stdcall WriteOutput(void* handle, const char* bytes, int count) noexcept
{
    auto& result = static_cast<Context*>(handle)->result;
    if (count <= 0) return 0;
    const size_t available = kLogLimit - result.diagnostics.size();
    const size_t requested = static_cast<size_t>(count);
    const size_t length = requested < available ? requested : available;
    try
    {
        result.diagnostics.append(bytes, length);
        if (length != requested) result.diagnostics_truncated = true;
    }
    catch (...)
    {
        // Исключение C++ не должно пересекать C ABI и повреждать стек движка.
        // Потеря диагностики не прерывает запись самого документа.
        result.diagnostics_truncated = true;
    }
    return count;
}

int __stdcall CheckCancel(void* handle) noexcept
{
    const auto* cancel = static_cast<Context*>(handle)->cancel;
    return cancel != nullptr && cancel->load(std::memory_order_relaxed) ? -1 : 0;
}

struct Instance
{
    Library& library;
    void* value = nullptr;
    bool needs_exit = false;
    ~Instance()
    {
        // Страховка всех ранних выходов: callbacks/context и DLL ещё живы.
        if (value != nullptr)
        {
            if (needs_exit) library.exit(value);
            library.destroy(value);
        }
    }
};

bool Encode(const std::wstring& text, std::string& output)
{
    if (text.find(L'\0') != std::wstring::npos || text.size() > INT_MAX) return false;
    if (text.empty()) { output.clear(); return true; }
    const int count = WideCharToMultiByte(CP_UTF8, WC_ERR_INVALID_CHARS, text.data(),
        static_cast<int>(text.size()), nullptr, 0, nullptr, nullptr);
    if (count == 0) return false;
    output.resize(static_cast<size_t>(count));
    return WideCharToMultiByte(CP_UTF8, WC_ERR_INVALID_CHARS, text.data(),
        static_cast<int>(text.size()), output.data(), count, nullptr, nullptr) == count;
}

bool SelectDll(const std::wstring& requested, Ghostscript::Result& result)
{
    if (!requested.empty())
    {
        result.dll_path = requested;
        return true;
    }
    // Путь отсчитывается от EXE, а не от рабочего каталога ярлыка/терминала.
    std::vector<wchar_t> executable(260);
    for (;;)
    {
        const DWORD length = GetModuleFileNameW(nullptr, executable.data(),
            static_cast<DWORD>(executable.size()));
        if (length == 0)
        {
            result.status = HRESULT_FROM_WIN32(GetLastError());
            return false;
        }
        if (length < executable.size())
        {
            result.dll_path = (std::filesystem::path(std::wstring(executable.data(), length))
                .parent_path() / L"runtime" / L"ghostscript" / L"bin" / L"gsdll64.dll").wstring();
            return true;
        }
        if (executable.size() >= 32768)
        {
            result.status = HRESULT_FROM_WIN32(ERROR_FILENAME_EXCED_RANGE);
            return false;
        }
        executable.resize(executable.size() * 2);
    }
}

Ghostscript::Result Execute(const std::wstring& path, const Ghostscript::RunOptions* options)
{
    Ghostscript::Result result;
    try
    {
        std::unique_lock lock(engine_mutex, std::try_to_lock);
        if (!lock.owns_lock())
        {
            result.status = HRESULT_FROM_WIN32(ERROR_BUSY);
            return result;
        }
        if (!SelectDll(path, result)) return result;
        // Аргументы готовятся до создания экземпляра. argv[0] служебный;
        // оболочка не используется, поэтому экранирование кавычками не нужно.
        std::vector<std::string> arguments{"BIC", "-dBATCH", "-dNOPAUSE", "-dSAFER"};
        std::vector<char*> argv;
        if (options != nullptr)
        {
            // Явные локальные пути ресурсов позволяют переносить весь комплект
            // без установки Ghostscript, его записей реестра и настройки GS_LIB.
            const auto root = std::filesystem::path(result.dll_path).parent_path().parent_path();
            const std::wstring resource_paths = L"-I" + (root / L"lib").wstring() + L";" +
                (root / L"Resource" / L"Init").wstring() + L";" +
                (root / L"Resource" / L"Font").wstring();
            std::string encoded_resources;
            if (!Encode(resource_paths, encoded_resources))
            {
                result.status = E_INVALIDARG;
                return result;
            }
            arguments.push_back(std::move(encoded_resources));
            // ICC-профили также принадлежат переносимому комплекту, а не пути
            // исходной установки/сборки Ghostscript. Завершающий разделитель обязателен.
            std::string encoded_profiles;
            if (!Encode(L"-sICCProfilesDir=" + (root / L"iccprofiles").wstring() + L"/", encoded_profiles))
            {
                result.status = E_INVALIDARG;
                return result;
            }
            arguments.push_back(std::move(encoded_profiles));
            if (options->arguments.empty() || options->arguments.size() > INT_MAX - arguments.size())
            {
                result.status = E_INVALIDARG;
                return result;
            }
            for (const auto& argument : options->arguments)
            {
                std::string encoded;
                if (!Encode(argument, encoded))
                {
                    result.status = E_INVALIDARG;
                    return result;
                }
                arguments.push_back(std::move(encoded));
            }
            for (auto& argument : arguments) argv.push_back(argument.data());
            argv.push_back(nullptr); // Не входит в argc; соответствует обычному argv.
            if (options->cancel != nullptr && options->cancel->load(std::memory_order_relaxed))
            {
                result.cancelled = true;
                result.status = HRESULT_FROM_WIN32(ERROR_CANCELLED);
                return result;
            }
        }
        Library library;
        if (!Load(result.dll_path, library, result)) return result;
        if (options == nullptr)
        {
            result.status = S_OK;
            result.stage = Ghostscript::Stage::Complete;
            return result;
        }
        Context context{result, options->cancel};
        Instance instance{library};
        result.stage = Ghostscript::Stage::Create;
        result.api_code = library.create(&instance.value, &context);
        if (result.api_code < 0 || instance.value == nullptr) return result;
        result.stage = Ghostscript::Stage::Configure;
        result.api_code = library.stdio(instance.value, ReadInput, WriteOutput, WriteOutput);
        if (result.api_code < 0) return result;
        result.api_code = library.poll(instance.value, CheckCancel);
        if (result.api_code < 0) return result;
        result.api_code = library.encoding(instance.value, kUtf8);
        if (result.api_code < 0) return result;
        result.stage = Ghostscript::Stage::Execute;
        instance.needs_exit = true;
        result.api_code = library.initialize(instance.value, static_cast<int>(arguments.size()), argv.data());
        // Info сообщает справку/версию до полной инициализации: exit не вызывается.
        if (result.api_code == kInfo) instance.needs_exit = false;
        else
        {
            result.exit_code = library.exit(instance.value);
            instance.needs_exit = false;
        }
        result.cancelled = result.api_code == kInterrupt && options->cancel != nullptr &&
            options->cancel->load(std::memory_order_relaxed);
        if (result.cancelled) result.status = HRESULT_FROM_WIN32(ERROR_CANCELLED);
        else if (result.api_code == 0 || result.api_code == kQuit || result.api_code == kInfo)
        {
            result.stage = Ghostscript::Stage::Exit;
            if (result.exit_code == 0 || result.exit_code == kQuit)
            {
                result.status = S_OK;
                result.stage = Ghostscript::Stage::Complete;
            }
        }
    }
    catch (const std::bad_alloc&) { result.status = E_OUTOFMEMORY; }
    catch (const std::filesystem::filesystem_error&) { result.status = E_INVALIDARG; }
    return result;
}
}

Ghostscript::Result Ghostscript::Inspect(const std::wstring& dll_path)
{
    return Execute(dll_path, nullptr);
}

Ghostscript::Result Ghostscript::Run(const RunOptions& options)
{
    return Execute(options.dll_path, &options);
}
