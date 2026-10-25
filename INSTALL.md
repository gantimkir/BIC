# Установка среды разработки BIC

Эта инструкция предназначена для новой Windows-машины. Проект собирается как
64-разрядное приложение Win32 на C++20 с помощью MSVC 2022, CMake и Ninja.
Debug-информация встраивается в объектные файлы ключом `/Z7`.

## 1. Системные требования

- Windows 10 или Windows 11 x64;
- учётная запись GitHub с доступом к репозиторию `gantimkir/BIC`;
- свободное место для Visual Studio Build Tools и отдельной папки сборки;
- PowerShell 5.1 или новее.

## 2. Установка Git

Установите [Git for Windows](https://git-scm.com/install/windows). Через
PowerShell это можно сделать командой:

```powershell
winget install --id Git.Git -e --source winget
```

Закройте и заново откройте PowerShell, затем проверьте установку:

```powershell
git --version
```

На новой машине один раз задайте имя и адрес электронной почты для коммитов:

```powershell
git config --global user.name "Ваше имя"
git config --global user.email "you@example.com"
```

## 3. Установка MSVC 2022, Windows SDK, CMake и Ninja

Скачайте **Build Tools for Visual Studio 2022 версии 17.x** со страницы
[истории выпусков Visual Studio 2022](https://learn.microsoft.com/visualstudio/releases/2022/release-history)
или из раздела [предыдущих выпусков](https://visualstudio.microsoft.com/vs/older-downloads/).
Также подходит установленный Visual Studio 2022 Community/Professional версии
17.x. Более новая основная версия Visual Studio 18.x не заменяет требуемую 17.x:
`build.ps1` намеренно ищет только Visual Studio 2022.

В Visual Studio Installer выберите рабочую нагрузку **Разработка классических
приложений на C++** (`Desktop development with C++`). В сведениях об установке
убедитесь, что отмечены:

- **MSVC v143 — VS 2022 C++ x64/x86 build tools**;
- актуальный **Windows 10 SDK** или **Windows 11 SDK**;
- **C++ CMake tools for Windows**.

Компонент CMake устанавливает необходимые для проекта CMake и Ninja. Официальное
описание установки доступно в документации
[Microsoft C++ Build Tools](https://learn.microsoft.com/cpp/overview/acquire-msvc)
и [CMake projects in Visual Studio](https://learn.microsoft.com/cpp/build/cmake-projects-in-visual-studio).

После установки откройте новый PowerShell. Самостоятельно запускать Developer
PowerShell не требуется: `build.ps1` находит Visual Studio 2022 через `vswhere`
и подключает среду MSVC автоматически.

## 4. Получение проекта

Рекомендуется иметь отдельный локальный клон на каждом компьютере и передавать
изменения через GitHub. Не синхронизируйте одну рабочую папку вместе с каталогом
`.git` одновременно между несколькими компьютерами.

Текущий рабочий каталог проекта — `C:\Projects\BIC` (D-038).
Если проект уже находится там, повторное клонирование не требуется.
На новой машине можно получить его так:

```powershell
New-Item -ItemType Directory -Force C:\Projects | Out-Null
git clone https://github.com/gantimkir/BIC.git C:\Projects\BIC
cd C:\Projects\BIC
```

Для приватного репозитория Git Credential Manager предложит войти в GitHub через
браузер. Пароль учётной записи GitHub в консоли не используется.

Проверьте состояние клона:

```powershell
git status
git remote -v
```

Для клона ожидается ветка `main`, связанная с `origin/main`, без локальных изменений.
Если папка перенесена копированием без `.git`, это не клон: локальная история и
связь с удалённой веткой отсутствуют. Не создавайте независимый первый коммит.
Сначала обеспечьте доступ к GitHub, получите `origin/main` и сопоставьте
перенесённые файлы с этой историей. Сгенерированный `build/` не добавляйте
в коммит; файлы `tmp/pdfs/*` уже отслеживаются в существующей истории.
В текущей папке 2026-10-22 локальная `main` привязана к `origin/main` (D-044);
изменены только файлы документации о переносе.

## 5. Сборка

### Машина с диском D:

Из корня проекта выполните:

```powershell
powershell.exe -NoProfile -ExecutionPolicy Bypass -File .\build.ps1
```

По умолчанию CMake создаст Debug-сборку здесь:

```text
D:\Dev\Build\BIC-Z7\debug\BIC.exe
```

### Машина без диска D:

Передайте абсолютный путь явно:

```powershell
powershell.exe -NoProfile -ExecutionPolicy Bypass -File .\build.ps1 `
    -BuildRoot C:\Dev\Build\BIC-Z7
```

Либо сохраните пользовательскую переменную окружения и перезапустите терминал:

```powershell
[Environment]::SetEnvironmentVariable(
    'BIC_BUILD_ROOT',
    'C:\Dev\Build\BIC-Z7',
    'User')
```

Папка сборки должна находиться вне каталога исходников. Не добавляйте её в Git.

## 6. Запуск и проверка

После успешной сборки запустите приложение:

```powershell
& 'D:\Dev\Build\BIC-Z7\debug\BIC.exe'
```

Для альтернативного корня используйте соответствующий путь. Должно открыться
обычное окно с заголовком `BIC`. Закройте его стандартной кнопкой окна.

Проверить, что используется встроенный формат отладочной информации `/Z7`, можно
в подробной команде компилятора:

```powershell
cmake --build --preset windows-debug --verbose
```

## 7. Получение и отправка изменений

Перед началом работы:

```powershell
git pull --ff-only
```

После изменений:

```powershell
git status
git add .
git commit -m "Краткое описание изменений"
git push
```

Перед `git pull` или переходом на другой компьютер сначала закоммитьте либо
уберите незавершённые изменения. Не работайте одновременно в одной
синхронизируемой копии проекта с двух компьютеров.

## 8. Частые проблемы

### Код 1421 при открытии сведений PDF

Код означает, что запущенный BIC.exe содержит старую разметку окна без новых
элементов навигации. Закройте BIC (включая значок в трее) и повторите Debug-сборку
в том же `BuildRoot`, из которого запускаете приложение. После изменения
`src/pdf/file_report.rc` или `src/pdf/resource.h` CMake пересобирает ресурс
`src/app.rc`; затем запустите обновлённый BIC.exe.

### Несовпадение исходного каталога в CMake-кэше после переноса проекта

Повторно запустите обновлённый `build.ps1` с тем же `-BuildRoot`, который
использовался при ошибке (для F5 он берётся из `bic.buildRoot` в VS Code).
Скрипт обнаружит прежний `CMAKE_HOME_DIRECTORY` и выполнит конфигурацию
с `--fresh`, затем сборку. CMake пересоздаст `CMakeCache.txt` и `CMakeFiles`.
Параметры, заданные только в старом кэше, потребуется задать заново;
например, `BIC_BUILD_TESTS` вернётся к `OFF`.
На текущей машине 2026-10-22 это проверено для `C:\Dev\Build\BIC-Z7\debug`:
`--fresh` сработал и Debug-сборка прошла.

### `Visual Studio Installer / vswhere not found`

Visual Studio 2022 или Build Tools установлены не полностью. Запустите Visual
Studio Installer и установите рабочую нагрузку разработки классических
приложений на C++.

### `Install Visual Studio 2022 C++ Build Tools with CMake`

В Visual Studio Installer добавьте компоненты **MSVC x64/x86** и
**C++ CMake tools for Windows**.

### `BuildRoot must be an absolute local path`

Передавайте полный путь с буквой диска, например:

```powershell
.\build.ps1 -BuildRoot C:\Dev\Build\BIC-Z7
```

### `detected dubious ownership` или сообщение о `safe.directory`

Сначала убедитесь, что это ваша доверенная копия проекта. Затем разрешите Git
работать именно с этим каталогом:

```powershell
git config --global --add safe.directory C:/Projects/BIC
```

Не добавляйте универсальное значение `*`.

### Ошибка авторизации GitHub

Повторите `git push` и войдите через окно Git Credential Manager. Если выбран
ручной HTTPS-вход, вместо пароля требуется персональный токен. Никогда не
сохраняйте токены в исходниках или командных файлах проекта.

## 9. Автономная поставка BIC с Ghostscript

Формат D-042 — переносимая папка, без отдельной установки Ghostscript пользователем.
Низкоуровневый API дополнен оконным сценарием растеризации (D-057).
Выбор PDF вызывает общий подсчёт Ghostscript и структурный анализ PDFium.
Из окна сведений доступна растеризация в новый PDF: 300 DPI / RGB JPEG 80%,
сравнение размеров и сохранение с суффиксом `_rastre` рядом с исходником
либо на рабочем столе. Исходный документ не изменяется.

В исходниках уже находится `third_party/ghostscript`: runtime Ghostscript 10.08.0
x64 (DLL, lib, Resource, iccprofiles, оригинальная документация), локальные DLL
Microsoft CRT и соответствующий исходный архив Ghostscript. Источники и лицензии
описаны в [памятке комплекта](third_party/ghostscript/README.md).
Не исключайте бинарные зависимости и исходный архив из передачи проекта.

Разработчик формирует переносимую поставку командой:

```powershell
.\build.ps1 -BuildRoot C:\Dev\Build\BIC-Z7 -Configuration Release -Portable
```

Скрипт использует отдельные preset `windows-release` и каталог `<BuildRoot>\release`,
затем `cmake --install` в `<BuildRoot>\portable`. Release BIC компонуется с /MT;
DLL Microsoft CRT для Ghostscript входят в runtime. Отсутствие обязательных
компонентов runtime или исходного архива останавливает Release-конфигурацию.
Debug остаётся прежним вариантом разработки с /MDd; его не распространять как portable.

Структура результата:

```text
portable/
  BIC.exe
  runtime/ghostscript/
    bin/gsdll64.dll, DLL Microsoft CRT
    lib/, Resource/, iccprofiles/, doc/
  runtime/pdfium/bin/pdfium.dll
  licenses/ghostscript/
    README.md
    source/ghostscript-10.08.0.tar.xz
  licenses/pdfium/
    LICENSE, README.md, VERSION, args.gn, third_party/
```

Пользователь копирует папку целиком на Windows 10/11 x64 и запускает BIC.exe.
Ghostscript, его EXE, установщик, PATH и GS_LIB ему не требуются. Путь DLL и
ресурсов строится относительно EXE, поэтому ярлык может иметь другую рабочую папку.
CMake копирует runtime и в обычный каталог сборки; путь источника комплекта
при необходимости задаётся `BIC_GHOSTSCRIPT_ROOT`.
При сборке синхронизируются только отличающиеся файлы комплекта. Если файл
нельзя обновить (например, он открыт другой программой), ошибка сборки содержит
его путь: закройте использующий файл процесс и повторите сборку.
Существующие команды Word/Excel сохраняют отдельную зависимость от Office.

С 2026-10-25 в исходниках также находится комплект `third_party/pdfium`
(DLL x64, используемые заголовки и лицензии). Он обязателен при конфигурации
Debug и Release. CMake копирует DLL в обычную сборку и в portable;
копирование пропускает совпадающую DLL. Отдельная установка PDFium не нужна.
Отсутствие или невозможность загрузки DLL при работе показывает недоступный
растровый счётчик, не заменяя его нулём. При обновлении переносите согласованные
DLL и заголовки вместе с лицензиями; источник версии указан в памятке комплекта.
Сборка, загрузка новой DLL и реальный анализ в этой задаче не выполнялись по D-018.
Для импорта/сохранения страниц используются комплектные `fpdf_ppo.h` и
`fpdf_save.h` той же версии. Новых DLL и установленного PDF-редактора не требуется.
Растеризация использует устройство `pdfimage24` из существующего Ghostscript.
Для временных source/converted/result PDF нужна доступная папка Windows TEMP;
для сохранения — доступ на запись в выбранное расположение. Совпадающее имя
выходного файла даёт отказ без перезаписи.

Ghostscript не устанавливался: официальный пакет распакован без запуска его
установщика. На текущей машине 2026-10-22 выполнены Debug-сборка и Release-сборка
с формированием portable. Загрузка DLL через BIC, вызовы API и работа на чистой
машине ещё не проверялись.
