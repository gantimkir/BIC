cmake_minimum_required(VERSION 3.25)

set(CMAKE_MSVC_DEBUG_INFORMATION_FORMAT
    "$<$<CONFIG:Debug,RelWithDebInfo>:Embedded>")

project(BIC VERSION 0.1.0 LANGUAGES CXX)


add_executable(BIC WIN32 src/main.cpp)
target_compile_features(BIC PRIVATE cxx_std_20)
target_compile_definitions(BIC PRIVATE UNICODE _UNICODE)
set_target_properties(BIC PROPERTIES CXX_EXTENSIONS OFF)
if(MSVC)
    target_compile_options(BIC PRIVATE /W4 /permissive- /utf-8)
endif()
