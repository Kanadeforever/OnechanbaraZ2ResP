# ============================================================
# Onechanbara Z2 — ASI 插件构建系统
# ============================================================
# 使用方法:
#   mkdir build && cd build
#   cmake .. -G "Visual Studio 17 2022" -A x64
#   cmake --build . --config Release
#
# MinHook 通过 FetchContent 自动下载，无需手动处理。
# ============================================================

cmake_minimum_required(VERSION 3.20)
project(OnechanbaraZ2_ASI VERSION 1.0 LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

include(FetchContent)

# ============================================================
# MinHook — 自动从 GitHub 下载
# ============================================================
FetchContent_Declare(
    minhook
    GIT_REPOSITORY https://github.com/TsudaKageyu/minhook.git
    GIT_TAG        master
    GIT_SHALLOW    TRUE
)
FetchContent_MakeAvailable(minhook)

set(MINHOOK_SRC_DIR "${minhook_SOURCE_DIR}/src")
set(MINHOOK_INC_DIR "${minhook_SOURCE_DIR}/include")

# ============================================================
# 通用编译选项（Release DLL + 静态 CRT）
# ============================================================
set(COMMON_COMPILE_OPTIONS
    /MT
    /O2
    /GS-
    /Gy
    /Zi
)

set(COMMON_LINK_OPTIONS
    /OPT:REF
    /OPT:ICF
    /DLL
)

# ============================================================
# 插件 1：OnechanbaraZ2_Resolution.asi
# 功能：动态修改游戏渲染分辨率 + 高 DPI 支持
# 依赖：无
# ============================================================
add_library(OnechanbaraZ2_Resolution SHARED
    OnechanbaraZ2_Resolution/main.cpp
)

target_compile_options(OnechanbaraZ2_Resolution PRIVATE ${COMMON_COMPILE_OPTIONS})
target_link_options(OnechanbaraZ2_Resolution PRIVATE ${COMMON_LINK_OPTIONS})
target_link_libraries(OnechanbaraZ2_Resolution PRIVATE kernel32.lib user32.lib)

set_target_properties(OnechanbaraZ2_Resolution PROPERTIES
    SUFFIX ".asi"
    PREFIX ""
)

# ============================================================
# 插件 2：OnechanbaraZ2_SaveRedirect.asi
# 功能：将存档目录重定向到 %USERPROFILE%\Saved Games\
# 依赖：MinHook（FetchContent 自动下载）
# ============================================================
add_library(OnechanbaraZ2_SaveRedirect SHARED
    OnechanbaraZ2_SaveRedirect/main.cpp
)

target_include_directories(OnechanbaraZ2_SaveRedirect PRIVATE
    ${MINHOOK_INC_DIR}
)

target_compile_options(OnechanbaraZ2_SaveRedirect PRIVATE ${COMMON_COMPILE_OPTIONS})
target_link_options(OnechanbaraZ2_SaveRedirect PRIVATE ${COMMON_LINK_OPTIONS})
target_link_libraries(OnechanbaraZ2_SaveRedirect PRIVATE
    minhook
    kernel32.lib
    shell32.lib
    shlwapi.lib
    ole32.lib
)

set_target_properties(OnechanbaraZ2_SaveRedirect PROPERTIES
    SUFFIX ".asi"
    PREFIX ""
)
