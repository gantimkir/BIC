# Ghostscript в автономной поставке BIC

Версия: Ghostscript 10.08.0, Windows x64, официальный выпуск Artifex gs10080.
Получено: 2026-10-18. Установщик Ghostscript не запускался: содержимое извлечено
архиватором. Ни Ghostscript EXE, ни его установщик в runtime не входят.

## Состав

- `runtime/bin/gsdll64.dll` — движок;
- `runtime/Resource`, `runtime/lib`, `runtime/iccprofiles` — ресурсы, шрифты и профили;
- `runtime/doc` — оригинальная документация и лицензия `COPYING`;
- `runtime/bin/*.dll` кроме Ghostscript — Microsoft VC143 CRT x64 14.44.35112
  из каталога Redist установленного Visual Studio 2022 Build Tools;
- `source/ghostscript-10.08.0.tar.xz` — официальный соответствующий исходный архив.

В папку поставки копируется runtime целиком; архив исходников и эта памятка
помещаются в `licenses/ghostscript`. DLL и ресурсы не изменены.
Для обновления заменять согласованный комплект, исходный архив и сведения ниже.
Разрядность BIC и DLL должна совпадать. Целевая ОС — Windows 10/11 x64.

## Источники и лицензии

[Официальный выпуск](https://github.com/ArtifexSoftware/ghostpdl-downloads/releases/tag/gs10080).

Исходный двоичный пакет:
https://github.com/ArtifexSoftware/ghostpdl-downloads/releases/download/gs10080/gs10080w64.exe

SHA-256 из метаданных официального выпуска (справочное значение, локальный хеш
в этой задаче не вычислялся по D-018):
`52a91b8bf09298788d7a57b9206127026c23eacd75405f0a131e26dc381dce50`.

Исходный архив:
https://github.com/ArtifexSoftware/ghostpdl-downloads/releases/download/gs10080/ghostscript-10.08.0.tar.xz

SHA-256 из тех же метаданных:
`c20492bc8ebb96c87fa2e52a0926e1cda8cde95d66145e018ac713fed5da38cf`.

Ghostscript — AGPLv3, если не приобретена отдельная коммерческая лицензия.
Полный текст находится в `runtime/doc/COPYING`; сведения о сторонних компонентах
сохранены в исходном архиве и оригинальной документации. Исходники BIC и права
получателей при распространении единой программы также должны соответствовать
применимым условиям AGPL. Наличие DLL не является исключением из copyleft.

Microsoft CRT не является AGPL-кодом: это разрешённые к распространению компоненты
Microsoft Visual Studio, используемые на условиях Microsoft Software License Terms.
[Описание локального развёртывания](https://learn.microsoft.com/en-us/cpp/windows/redistributing-visual-cpp-files).
Системные DLL Windows из System32 не копировались.

7-Zip применялся только для извлечения официальной поставки; не входит в комплект BIC
и не нужен пользователю или обычной сборке проекта.
