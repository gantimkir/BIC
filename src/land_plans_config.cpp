#include "land_plans_config.h"
#include <algorithm>
#include <fstream>

namespace
{
constexpr size_t kMaxBytes = 1024 * 1024;
bool Read(const std::filesystem::path& path, std::string& bytes, bool& exists, std::wstring& error)
{
    const DWORD attributes = GetFileAttributesW(path.c_str());
    exists = attributes != INVALID_FILE_ATTRIBUTES;
    if (!exists)
    {
        const DWORD code = GetLastError();
        if (code == ERROR_FILE_NOT_FOUND || code == ERROR_PATH_NOT_FOUND) return true;
        error = L"Не удалось прочитать настройку:\n" + path.wstring();
        return false;
    }
    std::ifstream input(path, std::ios::binary | std::ios::ate);
    if (!input || input.tellg() < 0 || input.tellg() > static_cast<std::streamoff>(kMaxBytes))
    {
        error = L"Настройка недоступна или превышает 1 МиБ:\n" + path.wstring();
        return false;
    }
    bytes.resize(static_cast<size_t>(input.tellg()));
    input.seekg(0);
    if (!bytes.empty() && !input.read(bytes.data(), static_cast<std::streamsize>(bytes.size())))
    {
        error = L"Ошибка чтения:\n" + path.wstring();
        return false;
    }
    return true;
}

std::wstring Trim(const std::wstring& text)
{
    const auto first = text.find_first_not_of(L" \t\r");
    return first == std::wstring::npos ? L"" :
        text.substr(first, text.find_last_not_of(L" \t\r") - first + 1);
}
}

bool LandPlansConfig::Normalize(Entry& entry, std::wstring& error)
{
    entry.value = Trim(entry.value);
    if (entry.value.empty() || entry.value.find_first_of(L"\r\n\t") != std::wstring::npos ||
        entry.value.find(L'\0') != std::wstring::npos)
    {
        error = L"Введите непустую строку без переводов строк и табуляций.";
        return false;
    }
    if (entry.separator)
    {
        if (entry.value.size() > 120)
        {
            error = L"Текст разделителя должен содержать не более 120 символов.";
            return false;
        }
        return true;
    }
    auto folder = std::filesystem::path(entry.value).lexically_normal();
    if (!folder.is_absolute())
    {
        error = L"Требуется абсолютный путь папки:\n" + entry.value;
        return false;
    }
    while (folder != folder.root_path() && folder.filename().empty()) folder = folder.parent_path();
    folder.make_preferred();
    entry.value = folder.wstring();
    return true;
}

std::wstring LandPlansConfig::Label(const Entry& entry)
{
    if (entry.separator) return entry.value;
    const auto name = std::filesystem::path(entry.value).filename().wstring();
    return name.empty() ? entry.value : name;
}

void LandPlansConfig::Sort(std::vector<Entry>& entries)
{
    // Разделители закреплены, поэтому сортировка не переносит папки в другую группу.
    auto begin = entries.begin();
    while (begin != entries.end())
    {
        if (begin->separator) { ++begin; continue; }
        const auto end = std::find_if(begin, entries.end(), [](const Entry& e) { return e.separator; });
        std::stable_sort(begin, end, [](const Entry& a, const Entry& b)
        {
            return CompareStringEx(L"ru-RU", NORM_IGNORECASE, Label(a).c_str(), -1,
                Label(b).c_str(), -1, nullptr, nullptr, 0) == CSTR_LESS_THAN;
        });
        begin = end;
    }
}

bool LandPlansConfig::Load(Snapshot& result, std::wstring& error)
{
    result = {};
    std::wstring module(32768, L'\0');
    const DWORD length = GetModuleFileNameW(nullptr, module.data(), static_cast<DWORD>(module.size()));
    if (length == 0 || length >= module.size()) { error = L"Не удалось определить папку BIC.exe."; return false; }
    module.resize(length);
    const auto directory = std::filesystem::path(module).parent_path();
    result.destination = directory / L"land_plans.user.txt";
    result.source = result.destination;
    if (!Read(result.source, result.original, result.existed, error)) return false;
    if (!result.existed)
    {
        result.source = directory / L"land_plans.txt";
        if (!Read(result.source, result.original, result.existed, error)) return false;
    }
    std::string bytes = result.original;
    if (bytes.compare(0, 3, "\xEF\xBB\xBF") == 0) bytes.erase(0, 3);
    if (bytes.empty()) return true;
    const int count = MultiByteToWideChar(CP_UTF8, MB_ERR_INVALID_CHARS, bytes.data(),
        static_cast<int>(bytes.size()), nullptr, 0);
    if (count == 0 || bytes.find('\0') != std::string::npos)
    {
        error = L"Сохраните настройку в UTF-8 без нулевых символов:\n" + result.source.wstring();
        return false;
    }
    std::wstring content(count, L'\0');
    MultiByteToWideChar(CP_UTF8, MB_ERR_INVALID_CHARS, bytes.data(), static_cast<int>(bytes.size()), content.data(), count);
    size_t begin = 0;
    while (begin < content.size())
    {
        const auto end = content.find(L'\n', begin);
        auto line = Trim(content.substr(begin, end == std::wstring::npos ? end : end - begin));
        if (!line.empty() && line.front() != L'#')
        {
            const bool separator = line.starts_with(L"@separator ");
            Entry entry{separator, separator ? line.substr(11) : line};
            if (!Normalize(entry, error)) return false;
            result.entries.push_back(entry);
        }
        if (end == std::wstring::npos) break;
        begin = end + 1;
    }
    return true;
}

bool LandPlansConfig::Save(const Snapshot& snapshot, const std::vector<Entry>& entries, std::wstring& error)
{
    // Проверка снимка предотвращает обычную потерю параллельных ручных правок.
    // Межпроцессную блокировку стороннего редактора этот протокол не обеспечивает.
    Snapshot current;
    if (!Load(current, error)) return false;
    if (current.source != snapshot.source || current.existed != snapshot.existed || current.original != snapshot.original)
    {
        error = L"Файл изменён вне редактора. Закройте редактор и откройте список заново.";
        return false;
    }
    std::wstring content = L"# Папки и группы меню BIC. UTF-8.\r\n";
    for (auto entry : entries)
    {
        if (!Normalize(entry, error)) return false;
        content += (entry.separator ? L"@separator " : L"") + entry.value + L"\r\n";
    }
    const int count = WideCharToMultiByte(CP_UTF8, WC_ERR_INVALID_CHARS, content.data(),
        static_cast<int>(content.size()), nullptr, 0, nullptr, nullptr);
    if (count <= 0 || count > static_cast<int>(kMaxBytes))
    {
        error = L"Список не может быть записан в UTF-8 или превышает 1 МиБ.";
        return false;
    }
    std::string bytes(count, '\0');
    WideCharToMultiByte(CP_UTF8, WC_ERR_INVALID_CHARS, content.data(), static_cast<int>(content.size()),
        bytes.data(), count, nullptr, nullptr);
    // CREATE_NEW исключает замену чужого временного файла; публикация в той же папке.
    std::filesystem::path temporary;
    HANDLE file = INVALID_HANDLE_VALUE;
    for (unsigned i = 0; i < 100 && file == INVALID_HANDLE_VALUE; ++i)
    {
        temporary = snapshot.destination.wstring() + L".tmp-" + std::to_wstring(GetCurrentProcessId()) + L"-" + std::to_wstring(i);
        file = CreateFileW(temporary.c_str(), GENERIC_WRITE, 0, nullptr, CREATE_NEW, FILE_ATTRIBUTE_NORMAL, nullptr);
        if (file == INVALID_HANDLE_VALUE && GetLastError() != ERROR_FILE_EXISTS) break;
    }
    if (file == INVALID_HANDLE_VALUE) { error = L"Нет доступа для записи рядом с BIC.exe."; return false; }
    DWORD written = 0;
    const bool saved = WriteFile(file, bytes.data(), static_cast<DWORD>(bytes.size()), &written, nullptr) &&
        written == bytes.size() && FlushFileBuffers(file);
    CloseHandle(file);
    if (!saved || !MoveFileExW(temporary.c_str(), snapshot.destination.c_str(), MOVEFILE_REPLACE_EXISTING | MOVEFILE_WRITE_THROUGH))
    {
        DeleteFileW(temporary.c_str());
        error = L"Не удалось сохранить список:\n" + snapshot.destination.wstring();
        return false;
    }
    return true;
}
