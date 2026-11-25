cmake_minimum_required(VERSION 3.24)
project(QuickLaunch VERSION 0.3.4 LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)
set(CMAKE_AUTOMOC ON)

find_package(Qt6 6.12 REQUIRED COMPONENTS Core Gui Qml Quick)
if(NOT Qt6_VERSION VERSION_LESS "6.13.0")
    message(FATAL_ERROR "The host Qt contract is pinned to Qt 6.12.x")
endif()

qt_standard_project_setup()
qt_policy(SET QTP0001 NEW)
qt_policy(SET QTP0004 NEW)

add_compile_definitions(NOMINMAX WIN32_LEAN_AND_MEAN UNICODE _UNICODE)
if(MSVC)
    add_compile_options(/utf-8 /EHsc /W3)
endif()

add_library(quicklaunch_core STATIC
    src/core/Models.h
    src/core/LauncherStore.h
    src/core/LauncherStore.cpp
    src/core/PinyinMatcher.h
    src/core/PinyinMatcher.cpp
    src/core/MayeNanoImporter.h
    src/core/MayeNanoImporter.cpp
    src/platform/ShellBridge.h
    src/platform/ShellBridge.cpp
    src/ui/ThemeObject.h
    src/ui/ThemeObject.cpp
)
target_include_directories(quicklaunch_core PUBLIC "${CMAKE_CURRENT_SOURCE_DIR}/src")
target_link_libraries(quicklaunch_core PUBLIC
    Qt6::Core
    Qt6::Gui
    shell32
    ole32
    oleaut32
    user32
    gdi32
    dwmapi
    advapi32
    uuid
)
if(MSVC)
    set_target_properties(quicklaunch_core PROPERTIES
        MSVC_RUNTIME_LIBRARY "MultiThreaded$<$<CONFIG:Debug>:Debug>DLL"
    )
endif()

qt_add_executable(QuickLaunch WIN32
    src/main.cpp
    src/protocol/Protocol.h
    src/protocol/Protocol.cpp
    src/platform/HotkeyService.h
    src/platform/HotkeyService.cpp
    src/platform/IconProvider.h
    src/platform/IconProvider.cpp
    src/ui/LauncherModels.h
    src/ui/LauncherModels.cpp
    src/ui/AppController.h
    src/ui/AppController.cpp
)
target_include_directories(QuickLaunch PRIVATE "${CMAKE_CURRENT_SOURCE_DIR}/src")
target_link_libraries(QuickLaunch PRIVATE
    quicklaunch_core
    Qt6::Core
    Qt6::Gui
    Qt6::Qml
    Qt6::Quick
)
if(MSVC)
    target_link_options(QuickLaunch PRIVATE /SUBSYSTEM:WINDOWS /ENTRY:mainCRTStartup)
    set_target_properties(QuickLaunch PROPERTIES
        MSVC_RUNTIME_LIBRARY "MultiThreaded$<$<CONFIG:Debug>:Debug>DLL"
    )
endif()

qt_add_resources(QuickLaunch "qml_res"
    PREFIX "/ql"
    BASE "${CMAKE_CURRENT_SOURCE_DIR}/qml"
    FILES
        qml/MainWindow.qml
        qml/ItemEditDialog.qml
        qml/SettingsDialog.qml
)

option(BUILD_TESTS "Build unit tests" ON)
if(BUILD_TESTS)
    qt_add_executable(quicklaunch_tests
        tests/CoreTests.cpp
    )
    target_include_directories(quicklaunch_tests PRIVATE "${CMAKE_CURRENT_SOURCE_DIR}/src")
    target_link_libraries(quicklaunch_tests PRIVATE
        quicklaunch_core
        Qt6::Core
        Qt6::Gui
    )
    if(MSVC)
        set_target_properties(quicklaunch_tests PROPERTIES
            MSVC_RUNTIME_LIBRARY "MultiThreaded$<$<CONFIG:Debug>:Debug>DLL"
        )
    endif()
endif()
