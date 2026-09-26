# Business Information Center (BIC)

Первый этап: минимальное оконное приложение Win32 на C++20. Трей и бизнес-функции будут добавлены позже.

## Файлы

- `src/main.cpp` — точка входа `wWinMain`, регистрация класса окна и цикл сообщений Win32.
- `CMakeLists.txt` — описание программы и настроек компилятора.
- `CMakePresets.json` — общая конфигурация Debug / Ninja.
- `build.ps1` — обнаружение MSVC 2022, конфигурация и сборка проекта.

## Сборка

Нужны Visual Studio 2022 Build Tools, MSVC x64/x86, Windows SDK и CMake/Ninja.
Путь установки инструментов определяется автоматически через vswhere.

Из PowerShell в папке проекта:

```powershell
powershell.exe -NoProfile -ExecutionPolicy Bypass -File .\build.ps1
```

По умолчанию файлы сборки размещаются в `D:\Dev\Build\BIC-Z7`.
Другой абсолютный путь можно передать параметром `-BuildRoot` или переменной окружения:

```powershell
[Environment]::SetEnvironmentVariable('BIC_BUILD_ROOT', 'C:\Dev\Build\BIC-Z7', 'User')
```

Исполняемый файл: `<BuildRoot>\debug\BIC.exe`.

Исходники храните в `C:\YD\Projects\BIC`. Дождитесь синхронизации перед переходом на другой компьютер.
Gitignore относится только к Git и не управляет облачной синхронизацией.
Для воспроизводимой сборки на компьютерах согласуйте точные версии MSVC, SDK и CMake.
Git-репозиторий пока не инициализирован.
