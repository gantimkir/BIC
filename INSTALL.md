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

Например, клонируйте проект в `C:\Dev\BIC`:

```powershell
New-Item -ItemType Directory -Force C:\Dev | Out-Null
git clone https://github.com/gantimkir/BIC.git C:\Dev\BIC
cd C:\Dev\BIC
```

Для приватного репозитория Git Credential Manager предложит войти в GitHub через
браузер. Пароль учётной записи GitHub в консоли не используется.

Проверьте состояние клона:

```powershell
git status
git remote -v
```

Ожидается ветка `main`, связанная с `origin/main`, без локальных изменений.

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
git config --global --add safe.directory C:/Dev/BIC
```

Не добавляйте универсальное значение `*`.

### Ошибка авторизации GitHub

Повторите `git push` и войдите через окно Git Credential Manager. Если выбран
ручной HTTPS-вход, вместо пароля требуется персональный токен. Никогда не
сохраняйте токены в исходниках или командных файлах проекта.
