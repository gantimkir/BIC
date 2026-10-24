# Синхронизирует комплект Ghostscript с каталогом рядом с BIC.exe.
# Скрипт запускается на каждой сборке: изменённые ресурсы должны попадать в Debug
# даже тогда, когда сам исполняемый файл не нуждается в повторной компоновке.
if(NOT DEFINED BIC_GHOSTSCRIPT_SOURCE OR NOT IS_DIRECTORY "${BIC_GHOSTSCRIPT_SOURCE}")
    message(FATAL_ERROR "Каталог исходного комплекта Ghostscript не найден: ${BIC_GHOSTSCRIPT_SOURCE}")
endif()
if(NOT DEFINED BIC_GHOSTSCRIPT_DESTINATION OR BIC_GHOSTSCRIPT_DESTINATION STREQUAL "")
    message(FATAL_ERROR "Не задан каталог назначения комплекта Ghostscript")
endif()

# Учитываем также каталоги: пустые папки комплекта должны сохраняться в сборке.
file(GLOB_RECURSE runtime_entries LIST_DIRECTORIES true
    RELATIVE "${BIC_GHOSTSCRIPT_SOURCE}" "${BIC_GHOSTSCRIPT_SOURCE}/*")
foreach(entry IN LISTS runtime_entries)
    set(source_path "${BIC_GHOSTSCRIPT_SOURCE}/${entry}")
    set(destination_path "${BIC_GHOSTSCRIPT_DESTINATION}/${entry}")
    if(IS_DIRECTORY "${source_path}")
        file(MAKE_DIRECTORY "${destination_path}")
    else()
        get_filename_component(destination_directory "${destination_path}" DIRECTORY)
        file(MAKE_DIRECTORY "${destination_directory}")
        # Сравнение содержимого не перезаписывает уже совпадающую DLL, которая
        # может быть загружена в работающий BIC. Ошибка называет конкретный файл.
        file(COPY_FILE "${source_path}" "${destination_path}"
            ONLY_IF_DIFFERENT RESULT copy_result)
        if(NOT copy_result STREQUAL "0")
            message(FATAL_ERROR
                "Не удалось скопировать Ghostscript '${entry}' в '${destination_path}': ${copy_result}")
        endif()
    endif()
endforeach()
