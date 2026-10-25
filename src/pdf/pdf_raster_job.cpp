#include "pdf_raster_job.h"

#include "ghostscript_engine.h"
#include "pdf_raster_analysis.h"

#include <shlobj.h>
#include <process.h>
#include <algorithm>
#include <filesystem>
#include <new>
#include <stdexcept>
#include <string_view>
#include <utility>
#include <vector>

namespace
{
// HANDLE закрывается при любом выходе, в том числе при исключении аллокации.
struct Handle
{
    HANDLE value = INVALID_HANDLE_VALUE;
    ~Handle() { if (value != INVALID_HANDLE_VALUE) { CloseHandle(value); } }
};

std::wstring Extended(const std::wstring& path)
{
    if (path.starts_with(L"\\\\?\\")) { return path; }
    if (path.starts_with(L"\\\\")) { return L"\\\\?\\UNC\\" + path.substr(2); }
    return L"\\\\?\\" + path;
}

std::wstring Child(const PdfRasterJob::Job& job, const wchar_t* name)
{
    return (std::filesystem::path(job.directory) / name).wstring();
}

std::wstring UniqueName()
{
    GUID id{};
    if (FAILED(CoCreateGuid(&id))) { throw std::runtime_error("CoCreateGuid"); }
    wchar_t text[40]{};
    StringFromGUID2(id, text, static_cast<int>(sizeof(text) / sizeof(text[0])));
    return L"BIC-" + std::wstring(text);
}

DWORD CreateWorkspace(PdfRasterJob::Job& job)
{
    std::vector<wchar_t> path(32768);
    const DWORD length = GetTempPathW(static_cast<DWORD>(path.size()), path.data());
    if (!length) { return GetLastError(); }
    if (length >= path.size()) { return ERROR_FILENAME_EXCED_RANGE; }
    const auto directory = std::filesystem::path(std::wstring(path.data(), length)) / UniqueName();
    // GUID-каталог создаётся эксклюзивно. Удаляются только собственные четыре
    // известных файла, рекурсивного удаления произвольных путей здесь нет.
    if (!CreateDirectoryW(Extended(directory.wstring()).c_str(), nullptr)) { return GetLastError(); }
    job.directory = directory.wstring();
    return ERROR_SUCCESS;
}

DWORD CopyBytes(HANDLE source, HANDLE destination, const std::atomic_bool& cancel)
{
    LARGE_INTEGER start{};
    if (!SetFilePointerEx(source, start, nullptr, FILE_BEGIN)) { return GetLastError(); }
    std::vector<unsigned char> buffer(1024 * 1024);
    for (;;)
    {
        if (cancel.load()) { return ERROR_CANCELLED; }
        DWORD count = 0;
        if (!ReadFile(source, buffer.data(), static_cast<DWORD>(buffer.size()), &count, nullptr))
        {
            return GetLastError();
        }
        if (!count) { break; }
        DWORD written = 0;
        if (!WriteFile(destination, buffer.data(), count, &written, nullptr)) { return GetLastError(); }
        if (written != count) { return ERROR_WRITE_FAULT; }
    }
    return FlushFileBuffers(destination) ? ERROR_SUCCESS : GetLastError();
}

bool SameSource(const BY_HANDLE_FILE_INFORMATION& left, const BY_HANDLE_FILE_INFORMATION& right)
{
    return left.dwVolumeSerialNumber == right.dwVolumeSerialNumber &&
        left.nFileIndexHigh == right.nFileIndexHigh && left.nFileIndexLow == right.nFileIndexLow &&
        left.nFileSizeHigh == right.nFileSizeHigh && left.nFileSizeLow == right.nFileSizeLow &&
        CompareFileTime(&left.ftLastWriteTime, &right.ftLastWriteTime) == 0;
}

DWORD CheckLocal(HANDLE file)
{
    std::vector<wchar_t> path(32768);
    const DWORD length = GetFinalPathNameByHandleW(file, path.data(),
        static_cast<DWORD>(path.size()), FILE_NAME_OPENED);
    if (!length) { return GetLastError(); }
    if (length >= path.size()) { return ERROR_FILENAME_EXCED_RANGE; }
    std::wstring_view final(path.data(), length);
    if (final.starts_with(L"\\\\?\\")) { final.remove_prefix(4); }
    if (final.size() < 3 || final[1] != L':' || final[2] != L'\\') { return ERROR_NETWORK_ACCESS_DENIED; }
    const wchar_t root[]{final[0], L':', L'\\', L'\0'};
    const UINT type = GetDriveTypeW(root);
    return type == DRIVE_FIXED || type == DRIVE_REMOVABLE || type == DRIVE_CDROM || type == DRIVE_RAMDISK
        ? ERROR_SUCCESS : ERROR_NETWORK_ACCESS_DENIED;
}

DWORD Snapshot(PdfRasterJob::Job& job)
{
    // До конца копирования запрещаем запись/удаление исходника. Если его уже
    // меняют, получаем обычную ошибку совместного доступа вместо рваного снимка.
    const Handle source{CreateFileW(Extended(job.source.path).c_str(), GENERIC_READ,
        FILE_SHARE_READ, nullptr, OPEN_EXISTING, FILE_FLAG_SEQUENTIAL_SCAN, nullptr)};
    if (source.value == INVALID_HANDLE_VALUE) { return GetLastError(); }
    DWORD error = CheckLocal(source.value);
    if (error != ERROR_SUCCESS) { return error; }
    BY_HANDLE_FILE_INFORMATION info{};
    if (!GetFileInformationByHandle(source.value, &info)) { return GetLastError(); }
    if (!job.source.source_identity || !SameSource(info, *job.source.source_identity))
    {
        return ERROR_FILE_INVALID; // Нужно повторно выбрать файл и обновить сведения.
    }
    job.before_bytes = (static_cast<std::uint64_t>(info.nFileSizeHigh) << 32) | info.nFileSizeLow;
    const Handle destination{CreateFileW(Extended(Child(job, L"source.pdf")).c_str(),
        GENERIC_WRITE, 0, nullptr, CREATE_NEW, FILE_ATTRIBUTE_NORMAL, nullptr)};
    if (destination.value == INVALID_HANDLE_VALUE) { return GetLastError(); }
    return CopyBytes(source.value, destination.value, job.cancel);
}

std::wstring SelectedPages(const std::vector<unsigned char>& mask)
{
    // PageList использует нумерацию с единицы. Диапазоны уменьшают строку для
    // длинных документов; значения формируются из индексов, не из внешнего текста.
    std::wstring pages;
    for (size_t index = 0; index < mask.size(); ++index)
    {
        if (mask[index]) { continue; }
        const size_t first = index;
        while (index + 1 < mask.size() && !mask[index + 1]) { ++index; }
        if (!pages.empty()) { pages += L","; }
        pages += std::to_wstring(first + 1);
        if (index != first) { pages += L"-" + std::to_wstring(index + 1); }
    }
    return pages;
}

std::wstring OutputPattern(const std::wstring& path)
{
    // Ghostscript трактует % как шаблон номера страницы даже в OutputFile.
    std::wstring escaped;
    for (const auto ch : path) { escaped += ch == L'%' ? L"%%" : std::wstring(1, ch); }
    return escaped;
}

DWORD Prepare(PdfRasterJob::Job& job)
{
    if (!job.source.page_count || !job.source.raster_page_count ||
        job.source.raster_pages.size() != static_cast<size_t>(*job.source.page_count) ||
        *job.source.raster_page_count >= *job.source.page_count) { return ERROR_INVALID_DATA; }
    DWORD error = CreateWorkspace(job);
    if (error != ERROR_SUCCESS) { return error; }
    error = Snapshot(job);
    if (error != ERROR_SUCCESS) { return error; }
    const Handle snapshot{CreateFileW(Extended(Child(job, L"source.pdf")).c_str(), GENERIC_READ,
        FILE_SHARE_READ, nullptr, OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, nullptr)};
    if (snapshot.value == INVALID_HANDLE_VALUE) { return GetLastError(); }
    job.phase.store(PdfRasterJob::Phase::Analyze);
    const auto analysis = PdfRasterAnalysis::CountRasterPages(snapshot.value, *job.source.page_count, job.cancel);
    if (!analysis.raster_pages) { return analysis.error; }
    if (analysis.page_is_raster != job.source.raster_pages) { return ERROR_FILE_INVALID; }
    job.phase.store(PdfRasterJob::Phase::Render);
    const auto input_path = Child(job, L"source.pdf");
    const auto converted_path = Child(job, L"converted.pdf");
    // Список разрешений Ghostscript разделяется ';'. Не расширяем доступ при
    // нестандартном имени TEMP: возвращаем понятную ошибку подготовки.
    if (input_path.find(L';') != std::wstring::npos) { return ERROR_INVALID_NAME; }
    const Ghostscript::RunOptions options{
        .arguments = {L"-dQUIET", L"-dPDFSTOPONERROR", L"-sDEVICE=pdfimage24",
            // pdfimage в комплектной версии сравнивает имена с учётом регистра:
            // допустимо JPEG, но не jpeg (gdevpdfimg.c: compression_strings).
            L"-r300", L"-dDownScaleFactor=1", L"-sCompression=JPEG", L"-dJPEGQ=80",
            L"-dUseCropBox", L"-dPrinted=false",
            L"-sPageList=" + SelectedPages(job.source.raster_pages),
            L"-sOutputFile=" + OutputPattern(converted_path),
            L"--permit-file-read=" + input_path, L"-f", input_path},
        .cancel = &job.cancel,
    };
    auto converted = Ghostscript::Run(options);
    if (!converted)
    {
        // Не теряем исходную причину за универсальным ERROR_INVALID_DATA.
        // Эти поля, как и error, GUI читает только после публикации done.
        job.engine_status = converted.status;
        job.engine_api_code = converted.api_code;
        job.engine_exit_code = converted.exit_code;
        job.engine_diagnostics = std::move(converted.diagnostics);
        if (converted.cancelled) { return ERROR_CANCELLED; }
        return HRESULT_FACILITY(converted.status) == FACILITY_WIN32
            ? static_cast<DWORD>(HRESULT_CODE(converted.status)) : ERROR_INVALID_DATA;
    }
    if (job.cancel.load()) { return ERROR_CANCELLED; }
    job.phase.store(PdfRasterJob::Phase::Combine);
    {
        const Handle rendered{CreateFileW(Extended(converted_path).c_str(), GENERIC_READ,
            FILE_SHARE_READ, nullptr, OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, nullptr)};
        if (rendered.value == INVALID_HANDLE_VALUE) { return GetLastError(); }
        const Handle output{CreateFileW(Extended(Child(job, L"result.pdf")).c_str(), GENERIC_WRITE,
            0, nullptr, CREATE_NEW, FILE_ATTRIBUTE_NORMAL, nullptr)};
        if (output.value == INVALID_HANDLE_VALUE) { return GetLastError(); }
        // Даже для полностью нерастрового файла сборка едина: проверяем число
        // импортируемых страниц и сохраняем именно их исходный порядок.
        error = PdfRasterAnalysis::CombinePages(snapshot.value, rendered.value, output.value,
            job.source.raster_pages, job.cancel);
        if (error != ERROR_SUCCESS) { return error; }
        if (!FlushFileBuffers(output.value)) { return GetLastError(); }
    }
    job.phase.store(PdfRasterJob::Phase::Validate);
    const Handle result{CreateFileW(Extended(Child(job, L"result.pdf")).c_str(), GENERIC_READ,
        FILE_SHARE_READ, nullptr, OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, nullptr)};
    if (result.value == INVALID_HANDLE_VALUE) { return GetLastError(); }
    const auto checked = PdfRasterAnalysis::CountRasterPages(result.value, *job.source.page_count, job.cancel);
    if (!checked.raster_pages) { return checked.error; }
    if (*checked.raster_pages != *job.source.page_count) { return ERROR_INVALID_DATA; }
    LARGE_INTEGER size{};
    if (!GetFileSizeEx(result.value, &size)) { return GetLastError(); }
    if (size.QuadPart <= 0) { return ERROR_INVALID_DATA; }
    job.after_bytes = static_cast<std::uint64_t>(size.QuadPart);
    job.ready = true;
    job.phase.store(PdfRasterJob::Phase::Ready);
    return ERROR_SUCCESS;
}

DWORD SaveResult(PdfRasterJob::Job& job)
{
    std::filesystem::path folder = std::filesystem::path(job.source.path).parent_path();
    if (job.save_to_desktop)
    {
        struct ShellPath { PWSTR value = nullptr; ~ShellPath() { CoTaskMemFree(value); } } desktop;
        const HRESULT result = SHGetKnownFolderPath(FOLDERID_Desktop, KF_FLAG_DEFAULT, nullptr, &desktop.value);
        if (FAILED(result) || desktop.value == nullptr) { return ERROR_PATH_NOT_FOUND; }
        folder = desktop.value; // Учитывает перенаправленный рабочий стол и OneDrive.
    }
    const auto destination = (folder / PdfRasterJob::OutputName(job.source)).wstring();
    const auto staging = (folder / (UniqueName() + L".tmp")).wstring();
    struct RemoveStaging
    {
        std::wstring path;
        ~RemoveStaging() { DeleteFileW(path.c_str()); }
    } cleanup{Extended(staging)};
    {
        const Handle source{CreateFileW(Extended(Child(job, L"result.pdf")).c_str(), GENERIC_READ,
            FILE_SHARE_READ, nullptr, OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, nullptr)};
        if (source.value == INVALID_HANDLE_VALUE) { return GetLastError(); }
        const Handle output{CreateFileW(cleanup.path.c_str(), GENERIC_WRITE, 0, nullptr,
            CREATE_NEW, FILE_ATTRIBUTE_NORMAL, nullptr)};
        if (output.value == INVALID_HANDLE_VALUE) { cleanup.path.clear(); return GetLastError(); }
        const DWORD error = CopyBytes(source.value, output.value, job.cancel);
        if (error != ERROR_SUCCESS) { return error; }
    }
    if (job.cancel.load()) { return ERROR_CANCELLED; }
    // Публикуем уже целый PDF атомарным переименованием на том же томе. Флаг
    // REPLACE_EXISTING не задаётся: существующий _rastre.pdf никогда не затирается.
    job.saved_path = destination; // Аллокация происходит до необратимой публикации.
    if (!MoveFileExW(cleanup.path.c_str(), Extended(destination).c_str(), MOVEFILE_WRITE_THROUGH))
    {
        job.saved_path.clear();
        return GetLastError();
    }
    job.phase.store(PdfRasterJob::Phase::Saved);
    return ERROR_SUCCESS;
}

unsigned __stdcall Worker(void* parameter) noexcept
{
    std::unique_ptr<std::shared_ptr<PdfRasterJob::Job>> argument(
        static_cast<std::shared_ptr<PdfRasterJob::Job>*>(parameter));
    const auto job = *argument;
    argument.reset();
    const bool saving = job->phase.load() == PdfRasterJob::Phase::Save;
    try
    {
        // Shell Known Folders вызываются на том же рабочем потоке. Инициализация
        // COM сбалансирована и не затрагивает STA-квартиру оконного потока.
        struct ComScope
        {
            HRESULT status = CoInitializeEx(nullptr, COINIT_MULTITHREADED);
            ~ComScope() { if (SUCCEEDED(status)) { CoUninitialize(); } }
        } com;
        if (FAILED(com.status)) { throw std::runtime_error("CoInitializeEx"); }
        job->error = saving ? SaveResult(*job) : Prepare(*job);
    }
    catch (const std::bad_alloc&) { job->error = ERROR_NOT_ENOUGH_MEMORY; }
    catch (const std::filesystem::filesystem_error&) { job->error = ERROR_INVALID_NAME; }
    catch (...) { job->error = ERROR_INVALID_DATA; }
    // Отмена на границе завершения подготовки также запрещает её результат.
    // После успешной публикации Save отмена уже не удаляет сохранённый PDF.
    if (job->cancel.load() && job->phase.load() != PdfRasterJob::Phase::Saved)
    {
        job->error = ERROR_CANCELLED;
        if (!saving) { job->ready = false; }
    }
    // Никаких записей в обычные поля после публикации: UI может тут же запустить Save.
    job->done.store(true, std::memory_order_release);
    return 0;
}

bool Launch(const std::shared_ptr<PdfRasterJob::Job>& job)
{
    auto argument = std::make_unique<std::shared_ptr<PdfRasterJob::Job>>(job);
    const uintptr_t thread = _beginthreadex(nullptr, 0, Worker, argument.get(), 0, nullptr);
    if (!thread) { return false; }
    argument.release();
    if (job->thread != nullptr) { CloseHandle(job->thread); }
    job->thread = reinterpret_cast<HANDLE>(thread);
    return true;
}
} // namespace

PdfRasterJob::Job::~Job()
{
    if (thread != nullptr) { CloseHandle(thread); }
    if (!directory.empty())
    {
        for (const auto* name : {L"source.pdf", L"converted.pdf", L"result.pdf"})
        {
            // Ошибка выделения строки при очистке не выходит из деструктора.
            try { DeleteFileW(Extended(Child(*this, name)).c_str()); } catch (...) {}
        }
        try { RemoveDirectoryW(Extended(directory).c_str()); } catch (...) {}
    }
}

std::shared_ptr<PdfRasterJob::Job> PdfRasterJob::Start(const Pdf::FileInfo& file)
{
    const auto job = std::make_shared<Job>();
    job->source = file;
    if (!Launch(job)) { throw std::runtime_error("_beginthreadex"); }
    return job;
}

bool PdfRasterJob::Save(const std::shared_ptr<Job>& job, bool desktop)
{
    if (!job || !job->done.load(std::memory_order_acquire) || !job->ready) { return false; }
    job->save_to_desktop = desktop;
    job->cancel.store(false);
    job->error = ERROR_SUCCESS;
    job->saved_path.clear();
    job->phase.store(Phase::Save);
    job->done.store(false, std::memory_order_release);
    try { if (Launch(job)) { return true; } } catch (...) {}
    job->error = ERROR_NOT_ENOUGH_MEMORY;
    job->done.store(true, std::memory_order_release);
    return false;
}

void PdfRasterJob::Cancel(const std::shared_ptr<Job>& job) noexcept
{
    if (job && !job->done.load(std::memory_order_acquire))
    {
        job->cancel.store(true);
        if (job->thread != nullptr) { CancelSynchronousIo(job->thread); }
    }
}

std::wstring PdfRasterJob::OutputName(const Pdf::FileInfo& file)
{
    return std::filesystem::path(file.path).stem().wstring() + L"_rastre.pdf";
}
