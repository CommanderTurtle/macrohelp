# =============================================================================
# CMake Build Configuration
# =============================================================================
# mkdir build && cd build
# cmake .. -G "Visual Studio 17 2022" -A x64
# cmake --build . --config Release
# =============================================================================

cmake_minimum_required(VERSION 3.20)
project(CursorOverlay VERSION 2.0.0 LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

# Enforce modern C++ conformance (VS 2022/2025+)
if(MSVC)
    add_compile_options(/W3 /permissive- /Zc:__cplusplus /EHsc)
    add_link_options(/SUBSYSTEM:WINDOWS)
endif()

set(SOURCES src/main.cpp)
set(RESOURCES res/CursorOverlay.rc)

add_executable(${PROJECT_NAME} WIN32 ${SOURCES} ${RESOURCES})

target_include_directories(${PROJECT_NAME} PRIVATE src)

target_compile_definitions(${PROJECT_NAME} PRIVATE
    UNICODE _UNICODE _WIN32_WINNT=0x0A00 WIN32_LEAN_AND_MEAN
)

target_link_libraries(${PROJECT_NAME} PRIVATE
    user32 gdi32 shell32 shlwapi gdiplus winhttp
)

set_target_properties(${PROJECT_NAME} PROPERTIES
    RUNTIME_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR}/bin
)
