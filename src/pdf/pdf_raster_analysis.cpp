#include "pdf_raster_analysis.h"

#include <fpdf_edit.h>
#include <fpdf_ppo.h>
#include <fpdf_save.h>

#include <climits>
#include <cmath>
#include <filesystem>
#include <mutex>
#include <new>
#include <string>
#include <utility>
#include <vector>

namespace
{
// PDFium не гарантирует потокобезопасность. Весь цикл библиотеки и документа
// проходит под одним mutex на вызывающем рабочем потоке, без ожидания второго входа.
std::mutex pdfium_mutex;
constexpr unsigned kMaxFormDepth = 64;
constexpr size_t kMaxObjects = 1000000;

struct Library
{
    HMODULE module = nullptr;
    bool initialized = false;
    decltype(&FPDF_InitLibraryWithConfig) initialize = nullptr;
    decltype(&FPDF_DestroyLibrary) destroy = nullptr;
    decltype(&FPDF_LoadCustomDocument) load_document = nullptr;
    decltype(&FPDF_CloseDocument) close_document = nullptr;
    decltype(&FPDF_GetLastError) last_error = nullptr;
    decltype(&FPDF_GetPageCount) page_count = nullptr;
    decltype(&FPDF_LoadPage) load_page = nullptr;
    decltype(&FPDF_ClosePage) close_page = nullptr;
    decltype(&FPDF_GetPageWidthF) page_width = nullptr;
    decltype(&FPDF_GetPageHeightF) page_height = nullptr;
    decltype(&FPDFPage_CountObjects) count_objects = nullptr;
    decltype(&FPDFPage_GetObject) get_object = nullptr;
    decltype(&FPDFPageObj_GetType) object_type = nullptr;
    decltype(&FPDFFormObj_CountObjects) count_form_objects = nullptr;
    decltype(&FPDFFormObj_GetObject) get_form_object = nullptr;
    decltype(&FPDF_CreateNewDocument) create_document = nullptr;
    decltype(&FPDF_ImportPagesByIndex) import_pages = nullptr;
    decltype(&FPDF_SaveAsCopy) save_document = nullptr;

    ~Library()
    {
        if (initialized) { destroy(); }
        if (module != nullptr) { FreeLibrary(module); }
    }
};

template<class Function>
bool Resolve(HMODULE module, const char* name, Function& function)
{
    // Типы взяты из заголовков именно комплектной версии; импортная LIB не нужна.
    function = reinterpret_cast<Function>(GetProcAddress(module, name));
    return function != nullptr;
}

DWORD LoadLibraryFromBundle(Library& library)
{
    std::vector<wchar_t> path(260);
    for (;;)
    {
        const DWORD length = GetModuleFileNameW(nullptr, path.data(), static_cast<DWORD>(path.size()));
        if (length == 0) { return GetLastError(); }
        if (length < path.size())
        {
            const auto dll = std::filesystem::path(std::wstring(path.data(), length)).parent_path() /
                L"runtime" / L"pdfium" / L"bin" / L"pdfium.dll";
            library.module = LoadLibraryExW(dll.c_str(), nullptr,
                LOAD_LIBRARY_SEARCH_DLL_LOAD_DIR | LOAD_LIBRARY_SEARCH_SYSTEM32);
            break;
        }
        if (path.size() >= 32768) { return ERROR_FILENAME_EXCED_RANGE; }
        path.resize(path.size() * 2);
    }
    if (library.module == nullptr) { return GetLastError(); }
    if (!Resolve(library.module, "FPDF_InitLibraryWithConfig", library.initialize) ||
        !Resolve(library.module, "FPDF_DestroyLibrary", library.destroy) ||
        !Resolve(library.module, "FPDF_LoadCustomDocument", library.load_document) ||
        !Resolve(library.module, "FPDF_CloseDocument", library.close_document) ||
        !Resolve(library.module, "FPDF_GetLastError", library.last_error) ||
        !Resolve(library.module, "FPDF_GetPageCount", library.page_count) ||
        !Resolve(library.module, "FPDF_LoadPage", library.load_page) ||
        !Resolve(library.module, "FPDF_ClosePage", library.close_page) ||
        !Resolve(library.module, "FPDF_GetPageWidthF", library.page_width) ||
        !Resolve(library.module, "FPDF_GetPageHeightF", library.page_height) ||
        !Resolve(library.module, "FPDFPage_CountObjects", library.count_objects) ||
        !Resolve(library.module, "FPDFPage_GetObject", library.get_object) ||
        !Resolve(library.module, "FPDFPageObj_GetType", library.object_type) ||
        !Resolve(library.module, "FPDFFormObj_CountObjects", library.count_form_objects) ||
        !Resolve(library.module, "FPDFFormObj_GetObject", library.get_form_object) ||
        !Resolve(library.module, "FPDF_CreateNewDocument", library.create_document) ||
        !Resolve(library.module, "FPDF_ImportPagesByIndex", library.import_pages) ||
        !Resolve(library.module, "FPDF_SaveAsCopy", library.save_document))
    {
        return ERROR_PROC_NOT_FOUND;
    }
    FPDF_LIBRARY_CONFIG config{};
    config.version = 2;
    library.initialize(&config);
    library.initialized = true;
    return ERROR_SUCCESS;
}

struct Input
{
    HANDLE file;
    const std::atomic_bool& cancel;
    DWORD error = ERROR_SUCCESS;
    ~Input() { if (file != INVALID_HANDLE_VALUE) { CloseHandle(file); } }
};

// Callback вызывается PDFium синхронно. Читаем уже открытый локальный файл,
// поэтому повторная подмена пути ссылкой не переключит анализ на другой файл.
int ReadBlock(void* parameter, unsigned long offset, unsigned char* buffer,
    unsigned long length) noexcept
{
    auto& input = *static_cast<Input*>(parameter);
    if (input.error != ERROR_SUCCESS) { return 0; }
    if (input.cancel.load())
    {
        input.error = ERROR_CANCELLED;
        return 0;
    }
    LARGE_INTEGER position{};
    position.QuadPart = offset;
    if (!SetFilePointerEx(input.file, position, nullptr, FILE_BEGIN))
    {
        input.error = GetLastError();
        return 0;
    }
    DWORD read = 0;
    if (!ReadFile(input.file, buffer, length, &read, nullptr))
    {
        input.error = GetLastError();
        return 0;
    }
    if (read != length)
    {
        input.error = ERROR_HANDLE_EOF;
        return 0;
    }
    return 1;
}

struct Document
{
    const Library& library;
    FPDF_DOCUMENT value;
    ~Document() { if (value != nullptr) { library.close_document(value); } }
};

struct Page
{
    const Library& library;
    FPDF_PAGE value;
    ~Page() { if (value != nullptr) { library.close_page(value); } }
};

struct Content
{
    bool image = false;
    bool other = false;
};

// Form — контейнер, а не вектор сам по себе. Несколько изображений/плиток тоже
// образуют растровую страницу. Любой текст, путь и shading исключают её, даже
// если объект невидим, белый, обрезан clipping-путём или находится под картинкой.
DWORD InspectObject(const Library& library, FPDF_PAGEOBJECT object, unsigned depth,
    size_t& remaining, Content& content, const std::atomic_bool& cancel)
{
    if (cancel.load()) { return ERROR_CANCELLED; }
    if (object == nullptr) { return ERROR_INVALID_DATA; }
    if (remaining == 0 || depth > kMaxFormDepth) { return ERROR_BUFFER_OVERFLOW; }
    --remaining;
    switch (library.object_type(object))
    {
    case FPDF_PAGEOBJ_IMAGE:
        content.image = true;
        return ERROR_SUCCESS;
    case FPDF_PAGEOBJ_TEXT:
    case FPDF_PAGEOBJ_PATH:
    case FPDF_PAGEOBJ_SHADING:
        content.other = true;
        return ERROR_SUCCESS;
    case FPDF_PAGEOBJ_FORM:
    {
        const int count = library.count_form_objects(object);
        if (count < 0) { return ERROR_INVALID_DATA; }
        for (int index = 0; index < count && !content.other; ++index)
        {
            const DWORD error = InspectObject(library,
                library.get_form_object(object, static_cast<unsigned long>(index)),
                depth + 1, remaining, content, cancel);
            if (error != ERROR_SUCCESS) { return error; }
        }
        return ERROR_SUCCESS;
    }
    default:
        // Неизвестный объект не считается доказательством отсутствия векторов.
        return ERROR_NOT_SUPPORTED;
    }
}
} // namespace

PdfRasterAnalysis::Result PdfRasterAnalysis::CountRasterPages(HANDLE file,
    int expected_pages, const std::atomic_bool& cancel)
{
    Result result;
    try
    {
        std::unique_lock lock(pdfium_mutex, std::try_to_lock);
        if (!lock.owns_lock()) { result.error = ERROR_BUSY; return result; }
        if (cancel.load()) { result.error = ERROR_CANCELLED; return result; }
        if (file == nullptr || file == INVALID_HANDLE_VALUE || expected_pages <= 0)
        {
            result.error = ERROR_INVALID_PARAMETER;
            return result;
        }
        // Исходный дескриптор читает только метаданные. ReOpenFile запрашивает
        // чтение того же объекта, сохраняя отдельную диагностику размера и анализа.
        Input input{ReOpenFile(file, GENERIC_READ,
            FILE_SHARE_READ | FILE_SHARE_WRITE | FILE_SHARE_DELETE, FILE_FLAG_RANDOM_ACCESS), cancel};
        if (input.file == INVALID_HANDLE_VALUE) { result.error = GetLastError(); return result; }
        LARGE_INTEGER size{};
        if (!GetFileSizeEx(input.file, &size)) { result.error = GetLastError(); return result; }
        // Публичный FPDF_FILEACCESS использует 32-разрядную длину в Windows.
        if (size.QuadPart <= 0) { result.error = ERROR_INVALID_DATA; return result; }
        if (static_cast<unsigned long long>(size.QuadPart) > ULONG_MAX)
        {
            result.error = ERROR_FILE_TOO_LARGE;
            return result;
        }
        Library library;
        result.error = LoadLibraryFromBundle(library);
        if (result.error != ERROR_SUCCESS) { return result; }
        FPDF_FILEACCESS access{};
        access.m_FileLen = static_cast<unsigned long>(size.QuadPart);
        access.m_GetBlock = ReadBlock;
        access.m_Param = &input;
        const Document document{library, library.load_document(&access, nullptr)};
        if (document.value == nullptr)
        {
            result.pdfium_error = library.last_error();
            result.error = input.error != ERROR_SUCCESS ? input.error : ERROR_INVALID_DATA;
            return result;
        }
        if (library.page_count(document.value) != expected_pages)
        {
            result.error = ERROR_INVALID_DATA;
            return result;
        }
        size_t remaining = kMaxObjects;
        int raster_pages = 0;
        std::vector<unsigned char> classification;
        classification.reserve(static_cast<size_t>(expected_pages));
        for (int index = 0; index < expected_pages; ++index)
        {
            if (cancel.load()) { result.error = ERROR_CANCELLED; return result; }
            const Page page{library, library.load_page(document.value, index)};
            if (page.value == nullptr)
            {
                result.pdfium_error = library.last_error();
                result.error = input.error != ERROR_SUCCESS ? input.error : ERROR_INVALID_DATA;
                return result;
            }
            const int count = library.count_objects(page.value);
            if (count < 0) { result.error = ERROR_INVALID_DATA; return result; }
            Content content;
            for (int object = 0; object < count && !content.other; ++object)
            {
                result.error = InspectObject(library, library.get_object(page.value, object),
                    0, remaining, content, cancel);
                if (result.error != ERROR_SUCCESS) { return result; }
            }
            if (input.error != ERROR_SUCCESS) { result.error = input.error; return result; }
            const bool raster = content.image && !content.other;
            classification.push_back(raster ? 1 : 0);
            if (raster) { ++raster_pages; }
        }
        if (cancel.load()) { result.error = ERROR_CANCELLED; return result; }
        result.raster_pages = raster_pages;
        result.page_is_raster = std::move(classification);
    }
    catch (const std::bad_alloc&) { result.error = ERROR_NOT_ENOUGH_MEMORY; }
    catch (const std::filesystem::filesystem_error&) { result.error = ERROR_INVALID_NAME; }
    return result;
}

namespace
{
// FILEWRITE — первый базовый подобъект: callback возвращается к владельцу
// безопасным static_cast. Никакие исключения не пересекают C ABI PDFium.
struct Output : FPDF_FILEWRITE
{
    HANDLE file;
    const std::atomic_bool& cancel;
    DWORD error = ERROR_SUCCESS;
    Output(HANDLE handle, const std::atomic_bool& stop) : file(handle), cancel(stop)
    {
        version = 1;
        WriteBlock = Write;
    }
    static int Write(FPDF_FILEWRITE* writer, const void* data, unsigned long size) noexcept
    {
        auto& output = *static_cast<Output*>(writer);
        if (output.error != ERROR_SUCCESS) { return 0; }
        if (output.cancel.load()) { output.error = ERROR_CANCELLED; return 0; }
        DWORD written = 0;
        if (!WriteFile(output.file, data, size, &written, nullptr))
        {
            output.error = GetLastError();
            return 0;
        }
        if (written != size) { output.error = ERROR_WRITE_FAULT; return 0; }
        return 1;
    }
};

DWORD SetupAccess(Input& input, FPDF_FILEACCESS& access)
{
    if (input.file == INVALID_HANDLE_VALUE) { return GetLastError(); }
    LARGE_INTEGER size{};
    if (!GetFileSizeEx(input.file, &size)) { return GetLastError(); }
    if (size.QuadPart <= 0) { return ERROR_INVALID_DATA; }
    if (static_cast<unsigned long long>(size.QuadPart) > ULONG_MAX) { return ERROR_FILE_TOO_LARGE; }
    access.m_FileLen = static_cast<unsigned long>(size.QuadPart);
    access.m_GetBlock = ReadBlock;
    access.m_Param = &input;
    return ERROR_SUCCESS;
}
} // namespace

DWORD PdfRasterAnalysis::CombinePages(HANDLE source, HANDLE converted, HANDLE output,
    const std::vector<unsigned char>& page_is_raster, const std::atomic_bool& cancel)
{
    std::unique_lock lock(pdfium_mutex, std::try_to_lock);
    if (!lock.owns_lock()) { return ERROR_BUSY; }
    if (cancel.load()) { return ERROR_CANCELLED; }
    if (page_is_raster.empty() || page_is_raster.size() > INT_MAX) { return ERROR_INVALID_PARAMETER; }
    Library library;
    DWORD error = LoadLibraryFromBundle(library);
    if (error != ERROR_SUCCESS) { return error; }
    Input original{ReOpenFile(source, GENERIC_READ, FILE_SHARE_READ, FILE_FLAG_RANDOM_ACCESS), cancel};
    FPDF_FILEACCESS original_access{};
    error = SetupAccess(original, original_access);
    if (error != ERROR_SUCCESS) { return error; }
    Input rendered{ReOpenFile(converted, GENERIC_READ, FILE_SHARE_READ, FILE_FLAG_RANDOM_ACCESS), cancel};
    FPDF_FILEACCESS rendered_access{};
    error = SetupAccess(rendered, rendered_access);
    if (error != ERROR_SUCCESS) { return error; }
    const Document original_doc{library, library.load_document(&original_access, nullptr)};
    const Document rendered_doc{library, library.load_document(&rendered_access, nullptr)};
    if (!original_doc.value || !rendered_doc.value) { return ERROR_INVALID_DATA; }
    const int pages = static_cast<int>(page_is_raster.size());
    int required = 0;
    for (const auto flag : page_is_raster)
    {
        if (flag > 1) { return ERROR_INVALID_PARAMETER; }
        if (flag == 0) { ++required; }
    }
    if (library.page_count(original_doc.value) != pages ||
        library.page_count(rendered_doc.value) != required) { return ERROR_INVALID_DATA; }
    const Document combined{library, library.create_document()};
    if (!combined.value) { return ERROR_NOT_ENOUGH_MEMORY; }
    int converted_index = 0;
    for (int index = 0; index < pages; ++index)
    {
        if (cancel.load()) { return ERROR_CANCELLED; }
        const bool keep = page_is_raster[static_cast<size_t>(index)] != 0;
        const int from = keep ? index : converted_index++;
        if (!keep)
        {
            const Page original_page{library, library.load_page(original_doc.value, index)};
            const Page rendered_page{library, library.load_page(rendered_doc.value, from)};
            if (!original_page.value || !rendered_page.value) { return ERROR_INVALID_DATA; }
            const float width = library.page_width(original_page.value);
            const float height = library.page_height(original_page.value);
            const float rendered_width = library.page_width(rendered_page.value);
            const float rendered_height = library.page_height(rendered_page.value);
            // CropBox и поворот должны давать прежний видимый размер страницы.
            // Допуск 1 pt учитывает округление к целому пикселю при 300 DPI.
            if (!std::isfinite(width) || !std::isfinite(height) ||
                !std::isfinite(rendered_width) || !std::isfinite(rendered_height) ||
                width <= 0 || height <= 0 || rendered_width <= 0 || rendered_height <= 0 ||
                std::fabs(width - rendered_width) > 1.0f ||
                std::fabs(height - rendered_height) > 1.0f) { return ERROR_INVALID_DATA; }
        }
        if (!library.import_pages(combined.value,
                keep ? original_doc.value : rendered_doc.value, &from, 1, index))
        {
            return ERROR_INVALID_DATA;
        }
    }
    if (original.error != ERROR_SUCCESS) { return original.error; }
    if (rendered.error != ERROR_SUCCESS) { return rendered.error; }
    Output writer(output, cancel);
    if (!library.save_document(combined.value, &writer, FPDF_NO_INCREMENTAL))
    {
        return writer.error != ERROR_SUCCESS ? writer.error : ERROR_WRITE_FAULT;
    }
    // Ленивое чтение импортированных потоков возможно и во время сохранения.
    if (original.error != ERROR_SUCCESS) { return original.error; }
    if (rendered.error != ERROR_SUCCESS) { return rendered.error; }
    return cancel.load() ? ERROR_CANCELLED : writer.error;
}
