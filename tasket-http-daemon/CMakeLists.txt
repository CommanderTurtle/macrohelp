cmake_minimum_required(VERSION 3.16)
project(TasketHttpTrigger VERSION 1.8.0 LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_AUTOMOC ON)

# ---------------------------------------------------------------------------
# Configuration
# ---------------------------------------------------------------------------
set(TASKETPP_ROOT "${CMAKE_CURRENT_SOURCE_DIR}/../original" CACHE PATH
    "Path to cloned Tasket++ source (git clone https://github.com/AmirHammouteneEI/ScheduledPasteAndKeys.git original)")

set(TASKET_HTTP_PORT 7777 CACHE STRING "Default HTTP listening port")
set(TASKET_HTTP_BIND "127.0.0.1" CACHE STRING "Default bind address")

# ---------------------------------------------------------------------------
# Qt dependencies
# ---------------------------------------------------------------------------
find_package(Qt6 REQUIRED COMPONENTS Core Gui Widgets Network)

# ---------------------------------------------------------------------------
# Platform check
# ---------------------------------------------------------------------------
if(NOT WIN32)
    message(FATAL_ERROR "Tasket++ HTTP Trigger requires Windows. Tasket++ actions use Win32 APIs (SendInput, clipboard, etc.).")
endif()

# ---------------------------------------------------------------------------
# Sidecar sources (new code — does NOT depend on TaskThread private APIs)
# ---------------------------------------------------------------------------
set(SIDECAR_SOURCES
    src/main.cpp
    src/HttpServer.cpp
    src/TaskJsonLoader.cpp
    src/TaskRunner.cpp
    src/TaskRegistry.cpp
    src/TaskExecutor.cpp        # Own executor — avoids TaskThread::copyActionsList() [private]
    src/mainwindow.cpp          # Stub for SystemCommandAction/RunningOtherTaskAction signals
)

set(SIDECAR_HEADERS
    include/Types.h
    include/HttpServer.h
    include/TaskJsonLoader.h
    include/TaskRunner.h
    include/TaskRegistry.h
    include/TaskExecutor.h
    include/mainwindow.h
    include/httplib.h
)

# ---------------------------------------------------------------------------
# Reused Tasket++ engine (NO UI code, NO TaskThread.cpp)
# ---------------------------------------------------------------------------
# NOTE: We deliberately exclude TaskThread.cpp because:
#   1. TaskThread::copyActionsList() is private (friend = TaskTab only)
#   2. TaskThread::stop() calls terminate() — forced, not graceful
# Our TaskExecutor class (above) reimplements the execution loop using
# only public AbstractAction APIs (deepCopy, runAction).
set(TASKET_ENGINE_SOURCES
    ${TASKETPP_ROOT}/Task.cpp
    ${TASKETPP_ROOT}/actions/AbstractAction.cpp
    ${TASKETPP_ROOT}/actions/ActionsTools.cpp
    ${TASKETPP_ROOT}/actions/PasteAction.cpp
    ${TASKETPP_ROOT}/actions/WaitAction.cpp
    ${TASKETPP_ROOT}/actions/KeysSequenceAction.cpp
    ${TASKETPP_ROOT}/actions/SystemCommandsAction.cpp
    ${TASKETPP_ROOT}/actions/CursorMovementsAction.cpp
    ${TASKETPP_ROOT}/actions/RunningOtherTaskAction.cpp
)

set(TASKET_ENGINE_HEADERS
    ${TASKETPP_ROOT}/Task.h
    ${TASKETPP_ROOT}/globals.h
    ${TASKETPP_ROOT}/actions/AbstractAction.h
    ${TASKETPP_ROOT}/actions/ActionParameters.h
    ${TASKETPP_ROOT}/actions/ActionsTools.h
    ${TASKETPP_ROOT}/actions/PasteAction.h
    ${TASKETPP_ROOT}/actions/WaitAction.h
    ${TASKETPP_ROOT}/actions/KeysSequenceAction.h
    ${TASKETPP_ROOT}/actions/SystemCommandsAction.h
    ${TASKETPP_ROOT}/actions/CursorMovementsAction.h
    ${TASKETPP_ROOT}/actions/RunningOtherTaskAction.h
)

# ---------------------------------------------------------------------------
# Executable
# ---------------------------------------------------------------------------
add_executable(tasket-httpd
    ${SIDECAR_SOURCES}
    ${SIDECAR_HEADERS}
    ${TASKET_ENGINE_SOURCES}
    ${TASKET_ENGINE_HEADERS}
)

# ---------------------------------------------------------------------------
# Include directories
# ---------------------------------------------------------------------------
target_include_directories(tasket-httpd PRIVATE
    include
    ${TASKETPP_ROOT}
    ${TASKETPP_ROOT}/actions
)

# ---------------------------------------------------------------------------
# Compile definitions
# ---------------------------------------------------------------------------
target_compile_definitions(tasket-httpd PRIVATE
    APP_VERSION=\"${PROJECT_VERSION}\"
    APP_NAME=\"Tasket++ HTTP Trigger\"
    TASKET_HTTP_PORT=${TASKET_HTTP_PORT}
    TASKET_HTTP_BIND=\"${TASKET_HTTP_BIND}\"
)

# ---------------------------------------------------------------------------
# Link libraries
# ---------------------------------------------------------------------------
target_link_libraries(tasket-httpd PRIVATE
    Qt::Core
    Qt::Gui
    Qt::Widgets
    Qt::Network
    user32
    ole32
    uuid
    shell32
)

# ---------------------------------------------------------------------------
# Compiler options
# ---------------------------------------------------------------------------
if(MSVC)
    target_compile_options(tasket-httpd PRIVATE /W3)
    set_property(TARGET tasket-httpd PROPERTY WIN32_EXECUTABLE FALSE)
else()
    target_compile_options(tasket-httpd PRIVATE
        -Wno-unused-function -Wno-unused-parameter)
endif()

# ---------------------------------------------------------------------------
# Install
# ---------------------------------------------------------------------------
install(TARGETS tasket-httpd RUNTIME DESTINATION .)
install(DIRECTORY saved_tasks/ DESTINATION saved_tasks OPTIONAL)

# ---------------------------------------------------------------------------
# CPack
# ---------------------------------------------------------------------------
set(CPACK_PACKAGE_NAME "Tasket++ HTTP Trigger")
set(CPACK_PACKAGE_VERSION ${PROJECT_VERSION})
set(CPACK_PACKAGE_VENDOR "Tasket++ Community")
set(CPACK_PACKAGE_DESCRIPTION_SUMMARY "LAN-only HTTP trigger daemon for Tasket++")
include(CPack)
