@echo off
echo [NetSpeedCtrl] 编译脚本 (使用 GCC/MinGW)
echo 为了能够顺利运行该程序，您需要安装 MinGW-w64 或者其他 GCC 编译器。
echo.

if not exist "WinDivert" (
    echo 找不到 WinDivert 文件夹，请确保您在网速限制目录下执行。
    pause
    exit /b 1
)

set GCC_CMD=gcc
set GCC_DIR=C:\Users\newnew\Downloads\x86_64-15.2.0-release-win32-seh-msvcrt-rt_v13-rev1\mingw64\bin
if exist "%GCC_DIR%\gcc.exe" (
    set "GCC_CMD=%GCC_DIR%\gcc.exe"
    echo 检测到您的自定义 GCC 路径，已经自动应用！
    echo 正在临时注入 PATH 防止汇编器 as.exe 丢失 DLL...
    set "PATH=%GCC_DIR%;%PATH%"
)

%GCC_CMD% src/main.c src/network.c -o NetSpeedCtrl.exe -I include -I WinDivert/include -L WinDivert/x64 -lWinDivert -luser32 -lgdi32 -lcomctl32 -lshlwapi -ladvapi32 -mwindows

if %ERRORLEVEL% EQU 0 (
    echo 编译成功！生成了 NetSpeedCtrl.exe
    echo 正在复制 WinDivert.dll 和 WinDivert64.sys...
    copy /y "WinDivert\x64\WinDivert.dll" .\
    copy /y "WinDivert\x64\WinDivert64.sys" .\
    echo.
    echo 启动前请确保以管理员权限运行 NetSpeedCtrl.exe，否则 WinDivert 驱动无法加载。
) else (
    echo 编译失败，请检查是否已安装 GCC 并将其加入了系统 PATH 环境变量中。
)

pause
