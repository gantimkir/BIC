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
    src/office_document.cpp
    src/office_document.h
    src/window_hotkeys.cpp
    src/window_hotkeys.h
    src/single_instance.cpp
    src/single_instance.h
    src/tray_icon.cpp
    src/tray_icon.h
    src/app.rc
    src/resource.h
)
target_include_directories(BIC PRIVATE "${CMAKE_CURRENT_SOURCE_DIR}/assets")
target_link_libraries(BIC PRIVATE shell32 ole32 oleaut32 uuid)
target_compile_features(BIC PRIVATE cxx_std_20)
target_compile_definitions(BIC PRIVATE UNICODE _UNICODE)
set_target_properties(BIC PROPERTIES CXX_EXTENSIONS OFF)
if(MSVC)
    target_compile_options(BIC PRIVATE /W4 /permissive- /utf-8)
endif()

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
