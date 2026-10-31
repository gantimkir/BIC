#pragma once
#include <windows.h>
#include <filesystem>
#include <string>
#include <vector>

namespace LandPlansConfig
{
struct Entry
{
    bool separator = false;
    std::wstring value; // Абсолютный путь папки либо текст заголовка-разделителя.
};
struct Snapshot
{
    std::vector<Entry> entries;
    std::filesystem::path source; // Активный файл: пользовательский имеет приоритет.
    std::filesystem::path destination; // Всегда land_plans.user.txt рядом с EXE.
    std::string original; // Исходные байты для обнаружения внешнего изменения.
    bool existed = false;
};
// Читает UTF-8; отсутствие обоих файлов даёт пустой список для первого сохранения.
// Ошибки возвращает строкой, частичный список при отказе использовать нельзя.
bool Load(Snapshot& result, std::wstring& error);
bool Normalize(Entry& entry, std::wstring& error);
std::wstring Label(const Entry& entry);
// Сортирует папки только между разделителями; порядок групп не меняется.
void Sort(std::vector<Entry>& entries);
// Публикует UTF-8 через временный файл. При внешнем изменении отказывает,
// не заменяет базовый land_plans.txt и не теряет исходник при ошибке записи.
bool Save(const Snapshot& snapshot, const std::vector<Entry>& entries, std::wstring& error);
}
