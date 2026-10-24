cmake_minimum_required(VERSION 3.25)

set(CMAKE_MSVC_DEBUG_INFORMATION_FORMAT
    "$<$<CONFIG:Debug,RelWithDebInfo>:Embedded>")

project(BIC VERSION 0.1.0 LANGUAGES CXX RC)


add_executable(BIC WIN32
    src/main.cpp
    src/main_window.cpp
    src/main_window.h
    src/contract_registry.cpp
    src/contract_registry.h
    src/das_fl_letterhead.cpp
    src/das_fl_letterhead.h
    src/das_ul_letterhead.cpp
    src/das_ul_letterhead.h
    src/ip_letterhead.cpp
    src/ip_letterhead.h
    src/ki_letterhead.cpp
    src/ki_letterhead.h
    src/pzz_61_48_text.cpp
    src/pzz_61_48_text.h
    src/pzz_61_41_text.cpp
    src/pzz_61_41_text.h
    src/pzz_61_48_graphics.cpp
    src/pzz_61_48_graphics.h
    src/pzz_61_41_graphics.cpp
    src/pzz_61_41_graphics.h
    src/pdf/file_drop_area.cpp
    src/pdf/file_drop_area.h
    src/pdf/pdf_report_dialog.cpp
    src/pdf/pdf_report_dialog.h
    src/pdf/pdf_progress_dialog.cpp
    src/pdf/pdf_progress_dialog.h
    src/pdf/pdf_file_info.h
    src/pdf/pdf_panel.cpp
    src/pdf/pdf_panel.h
    src/office_document.cpp
    src/office_document.h
    src/pdf/ghostscript_engine.cpp
    src/pdf/ghostscript_engine.h
    src/window_hotkeys.cpp
    src/window_hotkeys.h
    src/single_instance.cpp
    src/single_instance.h
    src/tray_icon.cpp
    src/tray_icon.h
    src/app.rc
    src/resource.h
    src/pdf/file_report.rc
    src/pdf/resource.h
)
# Диалог включён в общий app.rc; отдельная компиляция дала бы дубли ID ресурсов.
set_source_files_properties(src/pdf/file_report.rc PROPERTIES HEADER_FILE_ONLY TRUE)
# На текущем генераторе RC-включения не сканируются для Ninja. Явные зависимости
# пересобирают app.rc.res при изменении разметки или ID внутри src/pdf.
set_property(SOURCE src/app.rc APPEND PROPERTY OBJECT_DEPENDS
    "${CMAKE_CURRENT_SOURCE_DIR}/src/pdf/file_report.rc"
    "${CMAKE_CURRENT_SOURCE_DIR}/src/pdf/resource.h")
target_include_directories(BIC PRIVATE "${CMAKE_CURRENT_SOURCE_DIR}/assets")
target_link_libraries(BIC PRIVATE shell32 ole32 oleaut32 uuid)
target_compile_features(BIC PRIVATE cxx_std_20)
target_compile_definitions(BIC PRIVATE UNICODE _UNICODE)
set_target_properties(BIC PROPERTIES CXX_EXTENSIONS OFF)
if(MSVC)
    target_compile_options(BIC PRIVATE /W4 /permissive- /utf-8)
    # Release переносится без установленного VC Redistributable для самого BIC.
    # Debug сохраняет прежний /MDd и предназначен для машины разработчика.
    set_property(TARGET BIC PROPERTY MSVC_RUNTIME_LIBRARY
        "$<IF:$<CONFIG:Release>,MultiThreaded,MultiThreaded$<$<CONFIG:Debug>:Debug>DLL>")
endif()

# Переносимая поставка PDF-движка. Это каталог внутри исходников или явно
# подготовленный комплект, а не поиск установленной программы на машине пользователя.
set(BIC_GHOSTSCRIPT_ROOT "${CMAKE_CURRENT_SOURCE_DIR}/third_party/ghostscript/runtime"
    CACHE PATH "Ghostscript portable runtime root (bin, lib and resources)")
option(BIC_REQUIRE_GHOSTSCRIPT "Fail if portable Ghostscript bundle is incomplete" OFF)
if(BIC_REQUIRE_GHOSTSCRIPT)
    foreach(component bin/gsdll64.dll bin/msvcp140.dll bin/vcruntime140.dll
            bin/vcruntime140_1.dll lib Resource/Init/gs_init.ps Resource/Font
            iccprofiles doc/COPYING)
        if(NOT EXISTS "${BIC_GHOSTSCRIPT_ROOT}/${component}")
            message(FATAL_ERROR "Incomplete Ghostscript bundle: ${BIC_GHOSTSCRIPT_ROOT}/${component}")
        endif()
    endforeach()
    if(NOT EXISTS "${CMAKE_CURRENT_SOURCE_DIR}/third_party/ghostscript/source/ghostscript-10.08.0.tar.xz")
        message(FATAL_ERROR "Ghostscript corresponding source archive is missing from third_party/ghostscript/source")
    endif()
endif()
if(EXISTS "${BIC_GHOSTSCRIPT_ROOT}/bin/gsdll64.dll")
    # Цель вызывается при каждой сборке, но скрипт записывает только изменённые файлы.
    # Так работающий процесс не мешает сборке, если загруженная DLL не менялась.
    add_custom_target(bic_ghostscript_runtime ALL
        COMMAND "${CMAKE_COMMAND}"
            "-DBIC_GHOSTSCRIPT_SOURCE=${BIC_GHOSTSCRIPT_ROOT}"
            "-DBIC_GHOSTSCRIPT_DESTINATION=$<TARGET_FILE_DIR:BIC>/runtime/ghostscript"
            -P "${CMAKE_CURRENT_SOURCE_DIR}/src/pdf/copy_ghostscript_runtime.cmake"
        COMMENT "Synchronizing bundled Ghostscript runtime"
        VERBATIM)
    add_dependencies(BIC bic_ghostscript_runtime)
    install(DIRECTORY "${BIC_GHOSTSCRIPT_ROOT}/" DESTINATION runtime/ghostscript)
else()
    if(BIC_REQUIRE_GHOSTSCRIPT)
        message(FATAL_ERROR "Bundled Ghostscript is missing: ${BIC_GHOSTSCRIPT_ROOT}/bin/gsdll64.dll")
    else()
        message(WARNING "Bundled Ghostscript is missing: ${BIC_GHOSTSCRIPT_ROOT}/bin/gsdll64.dll. PDF API will be unavailable.")
    endif()
endif()
install(TARGETS BIC RUNTIME DESTINATION .)
install(DIRECTORY "${CMAKE_CURRENT_SOURCE_DIR}/third_party/ghostscript/source/"
    DESTINATION licenses/ghostscript/source)
install(FILES "${CMAKE_CURRENT_SOURCE_DIR}/third_party/ghostscript/README.md"
    DESTINATION licenses/ghostscript)

option(BIC_BUILD_TESTS "Build Office parameter validation tests" OFF)
if(BIC_BUILD_TESTS)
    enable_testing()
    add_executable(office_document_validation tests/office_document_validation.cpp src/office_document.cpp)
    target_include_directories(office_document_validation PRIVATE src)
    target_compile_features(office_document_validation PRIVATE cxx_std_20)
    target_compile_definitions(office_document_validation PRIVATE UNICODE _UNICODE)
    target_link_libraries(office_document_validation PRIVATE ole32 oleaut32 uuid user32)
    if(MSVC)
        target_compile_options(office_document_validation PRIVATE /W4 /permissive- /utf-8)
    endif()
    add_test(NAME office_document_validation COMMAND office_document_validation)
endif()
