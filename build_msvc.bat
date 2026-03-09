@echo off
echo [NetSpeedCtrl] 编译脚本 (使用 Visual Studio MSVC)
echo 请在 "Developer Command Prompt for VS" 或 "x64 Native Tools Command Prompt" 下运行此脚本。
echo.

if not exist "WinDivert" (
    echo 找不到 WinDivert 文件夹，请确保您在网速限制目录下执行。
    pause
    exit /b 1
)

cl.exe /O2 /Fe"NetSpeedCtrl.exe" src\main.c src\network.c src\utils.c /I"include" /I"WinDivert\include" /link /LIBPATH:"WinDivert\x64" WinDivert.lib user32.lib gdi32.lib comctl32.lib advapi32.lib shlwapi.lib

if %ERRORLEVEL% EQU 0 (
    echo 编译成功！生成了 NetSpeedCtrl.exe
    echo 正在复制 WinDivert.dll 和 WinDivert64.sys...
    copy /y "WinDivert\x64\WinDivert.dll" .\
    copy /y "WinDivert\x64\WinDivert64.sys" .\
    echo.
    echo 启动前请确保以管理员权限运行 NetSpeedCtrl.exe，否则 WinDivert 驱动无法加载。
    
    del *.obj
) else (
    echo 编译失败，请确保您正在 Visual Studio 开发者命令提示符中运行，且已安装 C++ 生成工具。
)

pause
