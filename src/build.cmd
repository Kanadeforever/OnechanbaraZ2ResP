@echo off
REM ============================================================
REM Onechanbara Z2 ASI 插件 — 一键编译脚本
REM
REM 前置条件：
REM   Visual Studio 2022（含 "使用 C++ 的桌面开发" 工作负载）
REM
REM 使用方法：
REM   1. 在 "Developer Command Prompt for VS 2022" 中运行此脚本
REM   2. 或直接双击运行（自动检测 VS 环境）
REM
REM 两个插件均零外部依赖，编译产物输出到 ..\build\
REM ============================================================

setlocal enabledelayedexpansion

echo ================================================================
echo  Onechanbara Z2 ASI Plugin Build Script
echo ================================================================

REM --- 检测 Visual Studio 环境 ---
if not defined VCINSTALLDIR (
    echo [INFO] 未检测到 VS 环境变量，尝试自动定位 VS 2022...

    set "VSWHERE=%ProgramFiles(x86)%\Microsoft Visual Studio\Installer\vswhere.exe"
    if exist "!VSWHERE!" (
        for /f "usebackq tokens=*" %%i in (`"!VSWHERE!" -latest -property installationPath`) do (
            set "VS_PATH=%%i"
        )
        if exist "!VS_PATH!\VC\Auxiliary\Build\vcvars64.bat" (
            call "!VS_PATH!\VC\Auxiliary\Build\vcvars64.bat"
            echo [INFO] VS found at: !VS_PATH!
        )
    )

    REM 回退：尝试常见安装路径
    if not defined VCINSTALLDIR (
        for %%d in (Community Professional Enterprise) do (
            if exist "C:\Program Files\Microsoft Visual Studio\2022\%%d\VC\Auxiliary\Build\vcvars64.bat" (
                call "C:\Program Files\Microsoft Visual Studio\2022\%%d\VC\Auxiliary\Build\vcvars64.bat"
                echo [INFO] VS 2022 %%d found
                goto :vs_found
            )
        )
        echo [ERROR] 未找到 Visual Studio 2022！
        echo         请安装 VS 2022 并在 Developer Command Prompt 中运行此脚本。
        exit /b 1
    )
    :vs_found
)

echo [INFO] 编译目标: Win64 Release
echo.

REM --- 创建输出目录 ---
if not exist "..\build" mkdir "..\build"

REM --- 编译开关 ---
set "CFLAGS=/nologo /std:c++17 /MT /O2 /GS- /Zi"
set "LFLAGS=/NODEFAULTLIB:libcmt.lib /OPT:REF /OPT:ICF /DLL"

REM ============================================================
REM [1/2] 编译分辨率插件
REM ============================================================
echo [1/2] 编译 OnechanbaraZ2_Resolution.asi ...

cl.exe %CFLAGS% ^
    /Fe:"..\build\OnechanbaraZ2_Resolution.asi" ^
    /LD ^
    /link %LFLAGS% kernel32.lib ^
    OnechanbaraZ2_Resolution\main.cpp

if errorlevel 1 (
    echo [FAIL] 分辨率插件编译失败！
) else (
    echo [ OK ] OnechanbaraZ2_Resolution.asi
    copy /Y OnechanbaraZ2_Resolution\OnechanbaraZ2_Resolution.ini "..\build\" >nul 2>&1
)

echo.

REM ============================================================
REM [2/2] 编译存档重定向插件
REM ============================================================
echo [2/2] 编译 OnechanbaraZ2_SaveRedirect.asi ...

cl.exe %CFLAGS% ^
    /Fe:"..\build\OnechanbaraZ2_SaveRedirect.asi" ^
    /LD ^
    /link %LFLAGS% kernel32.lib shell32.lib shlwapi.lib ole32.lib ^
    OnechanbaraZ2_SaveRedirect\main.cpp

if errorlevel 1 (
    echo [FAIL] 存档重定向插件编译失败！
) else (
    echo [ OK ] OnechanbaraZ2_SaveRedirect.asi
    copy /Y OnechanbaraZ2_SaveRedirect\OnechanbaraZ2_SaveRedirect.ini "..\build\" >nul 2>&1
)

echo.
echo ================================================================
echo  编译完成！输出目录: workspace\build\
echo ================================================================
dir /b "..\build\*.asi" "..\build\*.ini" 2>nul
echo.

endlocal
