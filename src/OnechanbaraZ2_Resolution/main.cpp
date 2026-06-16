/**
 * OnechanbaraZ2_Resolution.asi
 *
 * 分辨率自定义 ASI 插件 — 替换静态 Hex 补丁 + 高 DPI 支持
 * 配合 Ultimate ASI Loader 加载，通过同名 .ini 文件配置分辨率
 *
 * 原理：
 *   1. DPI 感知设置（早期，防止 Windows 缩放干扰）
 *   2. DllMain 阶段将游戏 EXE 中 5 处硬编码的 1920×1080
 *      分辨率常量替换为用户指定的宽/高值（默认 2560×1440）。
 *
 * Build: x64 DLL, rename to .asi
 * Usage: 放入游戏目录，与 OnechanbaraZ2_Resolution.ini 一起使用
 */

#include <windows.h>
#include <stdio.h>
#include <stdlib.h>

// DPI 感知所需类型定义（避免依赖最新 SDK）
#ifndef DPI_AWARENESS_CONTEXT_PER_MONITOR_AWARE_V2
#define DPI_AWARENESS_CONTEXT_PER_MONITOR_AWARE_V2 ((DPI_AWARENESS_CONTEXT)-4)
#endif

#ifndef PROCESS_PER_MONITOR_DPI_AWARE
#define PROCESS_PER_MONITOR_DPI_AWARE 2
#endif

// ============================================================
// 分辨率补丁位置定义
// 每个条目包含：RVA（相对虚拟地址）、标签、类型（0=高度, 1=宽度）
//
// 原始值均为 0x780 (1920 宽) 和 0x438 (1080 高)
// 所有偏移均经过三版本 EXE（原版/2K/4K）Hex 对比验证
// ============================================================

struct ResolutionPatchEntry {
    DWORD rva;          // 相对虚拟地址（距 ImageBase）
    const char* label;  // 调试标签
    int type;           // 0 = 高度, 1 = 宽度
};

static const ResolutionPatchEntry kPatches[] = {
    // RES_A: 渲染初始化函数 A — mov r9d, height; mov r8d, width
    { 0x00206113, "RenderInit_A_Height", 0 },
    { 0x00206119, "RenderInit_A_Width",  1 },

    // RES_B: 渲染初始化函数 B — mov r9d, height; mov r8d, width
    { 0x0020698F, "RenderInit_B_Height", 0 },
    { 0x00206995, "RenderInit_B_Width",  1 },

    // RES_C: 渲染初始化函数 C — mov r9d, height; mov r8d, width
    { 0x00206DF8, "RenderInit_C_Height", 0 },
    { 0x00206DFE, "RenderInit_C_Width",  1 },

    // RES_D: 渲染模式设置 — 注意此处的宽高顺序与 A/B/C 相反
    //        mov edx, width; mov r8d, height
    { 0x00206E32, "RenderMode_Width",  1 },
    { 0x00206E38, "RenderMode_Height", 0 },

    // RES_E: 分辨率枚举表 #1（栈上构建的 mov 立即数）
    { 0x004187AE, "ResTable1_Width",  1 },
    { 0x00418806, "ResTable1_Height", 0 },

    // RES_F: 分辨率枚举表 #2（另一份拷贝，相同结构）
    { 0x004174D2, "ResTable2_Width",  1 },
    { 0x00417525, "ResTable2_Height", 0 },

    // RES_G: 窗口创建 — 客户区尺寸（影响窗口化模式下的窗口大小）
    //        mov [rbp-0x28], width; ...; mov [rbp-0x28], height
    { 0x00421790, "WindowClient_Width",  1 },
    { 0x004217DA, "WindowClient_Height", 0 },
};

static const int kPatchCount = sizeof(kPatches) / sizeof(kPatches[0]);
static const DWORD kDefaultWidth  = 1920;
static const DWORD kDefaultHeight = 1080;

// ============================================================
// INI 文件读取（简化实现，无需链接任何库）
// ============================================================

static char g_ini_path[MAX_PATH] = {0};
static char g_log_path[MAX_PATH] = {0};
static bool g_log_enabled = true;   // 默认开启，INI 可关闭

static void BuildPaths(HMODULE hModule) {
    // 获取 ASI 文件自身的完整路径
    char dll_path[MAX_PATH];
    GetModuleFileNameA(hModule, dll_path, MAX_PATH);

    // 构建 .ini 路径
    strcpy_s(g_ini_path, MAX_PATH, dll_path);
    char* dot = strrchr(g_ini_path, '.');
    if (dot) strcpy_s(dot, MAX_PATH - (dot - g_ini_path), ".ini");

    // 构建 .log 路径（与 ASI 同名）
    strcpy_s(g_log_path, MAX_PATH, dll_path);
    dot = strrchr(g_log_path, '.');
    if (dot) strcpy_s(dot, MAX_PATH - (dot - g_log_path), ".log");
}

static DWORD ReadIniInt(const char* section, const char* key, DWORD default_val) {
    return GetPrivateProfileIntA(section, key, default_val, g_ini_path);
}

/**
 * 如果 INI 文件不存在，自动创建并写入默认值
 * 这样用户只需要分发 .asi 一个文件即可
 */
static void CreateDefaultIniIfNeeded() {
    if (GetFileAttributesA(g_ini_path) != INVALID_FILE_ATTRIBUTES) return;

    // 手动写入带注释的 INI
    FILE* f = fopen(g_ini_path, "w");
    if (!f) return;

    fprintf(f, "; ============================================================\n");
    fprintf(f, "; OnechanbaraZ2_Resolution.ini\n");
    fprintf(f, ";\n");
    fprintf(f, "; 分辨率自定义配置文件\n");
    fprintf(f, "; 配合 OnechanbaraZ2_Resolution.asi 使用\n");
    fprintf(f, "; ============================================================\n");
    fprintf(f, "[Resolution]\n");
    fprintf(f, "; 目标宽度（默认 1920 = 1080p）\n");
    fprintf(f, "; 常见值: 1920 (1080p), 2560 (2K), 3840 (4K)\n");
    fprintf(f, "Width=1920\n");
    fprintf(f, "; 目标高度（默认 1080 = 1080p）\n");
    fprintf(f, "; 常见值: 1080 (1080p), 1440 (2K), 2160 (4K)\n");
    fprintf(f, "Height=1080\n");
    fprintf(f, "\n");
    fprintf(f, "[DPI]\n");
    fprintf(f, "; 高 DPI 感知模式（默认启用）\n");
    fprintf(f, "; 防止 Windows 对游戏窗口的位图拉伸，让渲染与显示分辨率对应\n");
    fprintf(f, ";   0 = 禁用\n");
    fprintf(f, ";   1 = 系统 DPI 感知（Vista+）\n");
    fprintf(f, ";   2 = 每显示器 DPI 感知（Win8.1+，默认/推荐）\n");
    fprintf(f, "AwarenessMode=2\n");
    fprintf(f, "\n");
    fprintf(f, "[Debug]\n");
    fprintf(f, "; 日志开关（日志文件与 ASI 同名，位于游戏目录）\n");
    fprintf(f, ";   0 = 关闭日志   1 = 开启日志（默认）\n");
    fprintf(f, "EnableLog=1\n");

    fclose(f);
}

// ============================================================
// 内存补丁核心
// ============================================================
// 注意：游戏 EXE 的 ImageBase 很小（0x00000001），已启用 ASLR。
// 实际基址必须通过 GetModuleHandle(NULL) 动态获取。

static bool PatchMemory(void* address, DWORD value) {
    DWORD old_protect;
    if (!VirtualProtect(address, sizeof(DWORD), PAGE_EXECUTE_READWRITE, &old_protect)) {
        return false;
    }

    *(DWORD*)address = value;

    DWORD unused;
    VirtualProtect(address, sizeof(DWORD), old_protect, &unused);
    return true;
}

static void LogToFile(const char* fmt, ...) {
    if (!g_log_enabled) return;

    char buf[1024];
    va_list args;
    va_start(args, fmt);
    vsnprintf(buf, sizeof(buf), fmt, args);
    va_end(args);

    // 首次写入用 "w" 清空旧日志，后续追加
    static bool first_write = true;
    FILE* f = fopen(g_log_path, first_write ? "w" : "a");
    if (f) {
        if (first_write) first_write = false;
        fprintf(f, "%s\n", buf);
        fclose(f);
    }
}

// ============================================================
// 高 DPI 感知设置
// ============================================================
// 在 Windows 高 DPI 显示器上（如 4K@150% 缩放），如果进程未声明
// DPI 感知，系统会对窗口进行位图拉伸，导致实际渲染分辨率与
// 显示分辨率不一致。通过声明 DPI 感知，让游戏直接以物理像素渲染。

typedef BOOL    (WINAPI *SetProcessDPIAware_t)(void);
typedef HRESULT (WINAPI *SetProcessDpiAwareness_t)(int);
typedef BOOL    (WINAPI *SetProcessDpiAwarenessContext_t)(DPI_AWARENESS_CONTEXT);

static void SetupDpiAwareness(int mode) {
    if (mode == 0) {
        LogToFile("[DPI] DPI awareness disabled by config");
        return;
    }

    HMODULE user32   = GetModuleHandleA("user32.dll");
    HMODULE shcore   = LoadLibraryA("shcore.dll");

    const char* mode_name = "unknown";

    // 模式 2：尝试每显示器 DPI 感知 (Per-Monitor DPI Aware)
    if (mode >= 2) {
        // 优先尝试 V2 (Win10 1703+)，提供最佳的每显示器缩放行为
        if (user32) {
            auto pSetProcessDpiAwarenessContext =
                (SetProcessDpiAwarenessContext_t)GetProcAddress(
                    user32, "SetProcessDpiAwarenessContext");
            if (pSetProcessDpiAwarenessContext) {
                if (pSetProcessDpiAwarenessContext(
                        DPI_AWARENESS_CONTEXT_PER_MONITOR_AWARE_V2)) {
                    LogToFile("[DPI] Per-monitor DPI awareness V2 enabled");
                    goto done;
                }
            }
        }

        // 回退 V1 (Win8.1+)
        if (shcore) {
            auto pSetProcessDpiAwareness =
                (SetProcessDpiAwareness_t)GetProcAddress(
                    shcore, "SetProcessDpiAwareness");
            if (pSetProcessDpiAwareness) {
                if (SUCCEEDED(pSetProcessDpiAwareness(
                        PROCESS_PER_MONITOR_DPI_AWARE))) {
                    LogToFile("[DPI] Per-monitor DPI awareness V1 enabled");
                    goto done;
                }
            }
        }
    }

    // 模式 1：回退到系统 DPI 感知 (Vista+)
    if (mode >= 1) {
        if (user32) {
            auto pSetProcessDPIAware =
                (SetProcessDPIAware_t)GetProcAddress(
                    user32, "SetProcessDPIAware");
            if (pSetProcessDPIAware) {
                if (pSetProcessDPIAware()) {
                    LogToFile("[DPI] System DPI awareness enabled");
                    goto done;
                }
            }
        }
        LogToFile("[DPI] WARNING: Failed to set DPI awareness (mode=%d)", mode);
        goto done;
    }

done:
    if (shcore && shcore != (HMODULE)INVALID_HANDLE_VALUE) {
        // 不卸载 shcore.dll，进程生命周期内需要
    }
}

// ============================================================
// DllMain 入口
// ============================================================

BOOL APIENTRY DllMain(HMODULE hModule, DWORD ul_reason_for_call, LPVOID lpReserved) {
    if (ul_reason_for_call != DLL_PROCESS_ATTACH) {
        return TRUE;
    }

    // 禁用 DLL 加载通知（减少系统开销）
    DisableThreadLibraryCalls(hModule);

    // --- 第一步：构建路径并读取 INI 配置 ---
    BuildPaths(hModule);

    // 日志开关（默认开启）
    g_log_enabled = (ReadIniInt("Debug", "EnableLog", 1) != 0);

    // --- 第二步：设置 DPI 感知（必须在任何窗口创建前） ---
    int dpi_mode = (int)ReadIniInt("DPI", "AwarenessMode", 2);
    if (dpi_mode < 0) dpi_mode = 0;
    if (dpi_mode > 2) dpi_mode = 2;

    // 自动创建默认 INI（如不存在），方便用户纯 .asi 分发
    CreateDefaultIniIfNeeded();

    SetupDpiAwareness(dpi_mode);

    DWORD target_width  = ReadIniInt("Resolution", "Width",  kDefaultWidth);
    DWORD target_height = ReadIniInt("Resolution", "Height", kDefaultHeight);

    // 基本合法性检查
    if (target_width < 640 || target_height < 480 ||
        target_width > 16384 || target_height > 16384) {
        LogToFile("[ResolutionASI] ERROR: Invalid resolution %lux%lu, using defaults",
                  target_width, target_height);
        target_width  = kDefaultWidth;
        target_height = kDefaultHeight;
    }

    // 获取游戏 EXE 的实际基址（ASLR 后的地址）
    HMODULE game_base = GetModuleHandle(NULL);
    if (!game_base) {
        LogToFile("[ResolutionASI] ERROR: Cannot get game base address");
        return TRUE;
    }

    BYTE* base = (BYTE*)game_base;

    LogToFile("[ResolutionASI] Loading resolution: %lux%lu", target_width, target_height);
    LogToFile("[ResolutionASI] Game base: 0x%p", base);

    // 遍历所有补丁位置，执行内存写入
    int success_count = 0;
    int fail_count = 0;

    for (int i = 0; i < kPatchCount; i++) {
        const ResolutionPatchEntry& entry = kPatches[i];
        DWORD value = (entry.type == 1) ? target_width : target_height;

        BYTE* target_addr = base + entry.rva;

        if (PatchMemory(target_addr, value)) {
            LogToFile("[ResolutionASI] [OK] %s @ RVA 0x%08X: wrote 0x%08X (%lu)",
                      entry.label, entry.rva, value, value);
            success_count++;
        } else {
            LogToFile("[ResolutionASI] [FAIL] %s @ RVA 0x%08X: VirtualProtect failed",
                      entry.label, entry.rva);
            fail_count++;
        }
    }

    LogToFile("[ResolutionASI] Patched %d/%d locations (%d failed)",
              success_count, kPatchCount, fail_count);

    return TRUE;
}
