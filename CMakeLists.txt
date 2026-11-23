cmake_minimum_required(VERSION 3.20)
project(WinBatteryBar LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

add_executable(WinBatteryBar WIN32
    src/main.cpp
    src/settings.cpp
    src/battery_reader.cpp
    src/taskbar_dock.cpp
    src/renderer.cpp
    src/tooltip_window.cpp
    src/widget_window.cpp
    app.rc
)

target_compile_definitions(WinBatteryBar PRIVATE
    UNICODE
    _UNICODE
    NOMINMAX
    WIN32_LEAN_AND_MEAN
)

if(MSVC)
    # Static CRT (/MT) so the single WinBatteryBar.exe has zero external runtime DLL dependencies
    set_property(TARGET WinBatteryBar PROPERTY MSVC_RUNTIME_LIBRARY "MultiThreaded")
    target_compile_options(WinBatteryBar PRIVATE
        /W4
        /EHsc
        /O2
        /utf-8
        /permissive-
    )
    # Manifest is already embedded via app.rc (ID 1, RT_MANIFEST)
    target_link_options(WinBatteryBar PRIVATE /MANIFEST:NO)
endif()

target_link_libraries(WinBatteryBar PRIVATE
    user32
    gdi32
    gdiplus
    setupapi
    advapi32
    ole32
    shell32
    dwmapi
    windowsapp
)
