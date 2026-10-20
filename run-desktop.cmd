@echo off
setlocal
rem Only this process and its child use the matching MSYS2 runtime and Qt plugins.
set "PATH=D:\msys64\ucrt64\bin;%PATH%"
set "QT_QPA_PLATFORM_PLUGIN_PATH=D:\msys64\ucrt64\share\qt6\plugins\platforms"
set "QT_QPA_PLATFORM=windows"
if not exist "%~dp0build\neko_desktop.exe" (
    echo Build the desktop program first:
    echo cmake -S . -B build
    echo cmake --build build --target neko_desktop
    pause
    exit /b 1
)
start "" "%~dp0build\neko_desktop.exe" %*
