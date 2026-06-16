/**
 * OnechanbaraZ2_SaveRedirect.asi
 *
 * 存档目录重定向 ASI 插件
 * 将游戏默认的 "savedata" 目录重定向到用户自定义路径
 * 默认目标：%USERPROFILE%\Saved Games\OnechanbaraZ2
 *
 * 原理：MinHook Windows 文件 API，路径含 "savedata" 时替换为目标路径。
 * Build: x64 DLL + MinHook, rename to .asi
 * Usage: 放入游戏目录，与 OnechanbaraZ2_SaveRedirect.ini 一起使用
 */

#include <windows.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <shlobj.h>
#include <knownfolders.h>
#include <shlwapi.h>
#include "MinHook.h"

#pragma comment(lib, "shell32.lib")
#pragma comment(lib, "shlwapi.lib")
#pragma comment(lib, "ole32.lib")

// ============================================================
// 全局状态
// ============================================================

static char g_target_path[MAX_PATH] = {0};
static char g_ini_path[MAX_PATH]    = {0};
static char g_log_path[MAX_PATH]    = {0};
static bool g_hooks_installed       = false;
static bool g_log_enabled           = true;

// MinHook 原始函数指针
typedef HANDLE (WINAPI *CreateFileA_fn)(LPCSTR, DWORD, DWORD, LPSECURITY_ATTRIBUTES, DWORD, DWORD, HANDLE);
typedef HANDLE (WINAPI *CreateFileW_fn)(LPCWSTR, DWORD, DWORD, LPSECURITY_ATTRIBUTES, DWORD, DWORD, HANDLE);
typedef BOOL   (WINAPI *CreateDirectoryA_fn)(LPCSTR, LPSECURITY_ATTRIBUTES);
typedef BOOL   (WINAPI *CreateDirectoryW_fn)(LPCWSTR, LPSECURITY_ATTRIBUTES);
typedef DWORD  (WINAPI *GetFileAttributesA_fn)(LPCSTR);
typedef DWORD  (WINAPI *GetFileAttributesW_fn)(LPCWSTR);
typedef BOOL   (WINAPI *DeleteFileA_fn)(LPCSTR);
typedef BOOL   (WINAPI *DeleteFileW_fn)(LPCWSTR);
typedef HANDLE (WINAPI *FindFirstFileA_fn)(LPCSTR, LPWIN32_FIND_DATAA);
typedef HANDLE (WINAPI *FindFirstFileW_fn)(LPCWSTR, LPWIN32_FIND_DATAW);

static CreateFileA_fn         g_orig_CreateFileA         = NULL;
static CreateFileW_fn         g_orig_CreateFileW         = NULL;
static CreateDirectoryA_fn    g_orig_CreateDirectoryA    = NULL;
static CreateDirectoryW_fn    g_orig_CreateDirectoryW    = NULL;
static GetFileAttributesA_fn  g_orig_GetFileAttributesA  = NULL;
static GetFileAttributesW_fn  g_orig_GetFileAttributesW  = NULL;
static DeleteFileA_fn         g_orig_DeleteFileA         = NULL;
static DeleteFileW_fn         g_orig_DeleteFileW         = NULL;
static FindFirstFileA_fn      g_orig_FindFirstFileA      = NULL;
static FindFirstFileW_fn      g_orig_FindFirstFileW      = NULL;

// ============================================================
// 日志功能
// ============================================================

static void LogInit(HMODULE hModule) {
    char dll_path[MAX_PATH];
    GetModuleFileNameA(hModule, dll_path, MAX_PATH);
    strcpy_s(g_log_path, MAX_PATH, dll_path);
    char* dot = strrchr(g_log_path, '.');
    if (dot) strcpy_s(dot, MAX_PATH - (dot - g_log_path), ".log");
}

static void LogWrite(const char* fmt, ...) {
    if (!g_log_enabled) return;

    char buf[2048];
    va_list args;
    va_start(args, fmt);
    vsnprintf(buf, sizeof(buf), fmt, args);
    va_end(args);

    static bool first_write = true;
    FILE* f = fopen(g_log_path, first_write ? "w" : "a");
    if (f) {
        if (first_write) first_write = false;
        SYSTEMTIME st;
        GetLocalTime(&st);
        fprintf(f, "[%02d:%02d:%02d.%03d] %s\n",
                st.wHour, st.wMinute, st.wSecond, st.wMilliseconds, buf);
        fclose(f);
    }
}

// ============================================================
// INI 配置读取
// ============================================================

static void BuildIniPath(HMODULE hModule) {
    char dll_path[MAX_PATH];
    GetModuleFileNameA(hModule, dll_path, MAX_PATH);
    strcpy_s(g_ini_path, MAX_PATH, dll_path);
    char* dot = strrchr(g_ini_path, '.');
    if (dot) strcpy_s(dot, MAX_PATH - (dot - g_ini_path), ".ini");
}

static DWORD ReadIniInt(const char* section, const char* key, DWORD default_val) {
    return GetPrivateProfileIntA(section, key, default_val, g_ini_path);
}

static void ReadIniString(const char* section, const char* key,
                          const char* default_val, char* out, int out_size) {
    GetPrivateProfileStringA(section, key, default_val, out, out_size, g_ini_path);
}

static void CreateDefaultIniIfNeeded() {
    if (GetFileAttributesA(g_ini_path) != INVALID_FILE_ATTRIBUTES) return;

    // 手动写入带注释的 INI，WritePrivateProfileStringA 不支持注释
    FILE* f = fopen(g_ini_path, "w");
    if (!f) return;

    fprintf(f, "; ============================================================\n");
    fprintf(f, "; OnechanbaraZ2_SaveRedirect.ini\n");
    fprintf(f, ";\n");
    fprintf(f, "; 存档目录重定向配置文件\n");
    fprintf(f, "; 配合 OnechanbaraZ2_SaveRedirect.asi 使用\n");
    fprintf(f, "; ============================================================\n");
    fprintf(f, "[SaveRedirect]\n");
    fprintf(f, "; 存档目标路径（支持环境变量，如 %%USERPROFILE%%）\n");
    fprintf(f, "; 游戏原始的 savedata 目录会被透明重定向到此路径\n");
    fprintf(f, "TargetPath=%%USERPROFILE%%\\Saved Games\\OnechanbaraZ2\n");
    fprintf(f, "\n");
    fprintf(f, "[Debug]\n");
    fprintf(f, "; 日志开关（日志文件与 ASI 同名，位于游戏目录）\n");
    fprintf(f, ";   0 = 关闭日志   1 = 开启日志（默认）\n");
    fprintf(f, "EnableLog=1\n");

    fclose(f);
}

// ============================================================
// 路径处理辅助函数
// ============================================================

static bool ExpandEnvString(const char* input, char* output, DWORD out_size) {
    DWORD result = ExpandEnvironmentStringsA(input, output, out_size);
    return (result > 0 && result <= out_size);
}

static bool EnsureDirectoryExists(const char* path) {
    DWORD attr = GetFileAttributesA(path);
    if (attr != INVALID_FILE_ATTRIBUTES && (attr & FILE_ATTRIBUTE_DIRECTORY))
        return true;

    char parent[MAX_PATH];
    strcpy_s(parent, MAX_PATH, path);
    char* last_slash = strrchr(parent, '\\');
    if (last_slash && last_slash != parent) {
        if (last_slash > parent + 2 || parent[1] != ':') {
            *last_slash = '\0';
            EnsureDirectoryExists(parent);
        }
    }

    CreateDirectoryA(path, NULL);
    attr = GetFileAttributesA(path);
    return (attr != INVALID_FILE_ATTRIBUTES && (attr & FILE_ATTRIBUTE_DIRECTORY));
}

static bool GetSavedGamesPath(char* out, DWORD out_size) {
    PWSTR wide_path = NULL;
    HRESULT hr = SHGetKnownFolderPath(FOLDERID_SavedGames, 0, NULL, &wide_path);
    if (SUCCEEDED(hr) && wide_path) {
        WideCharToMultiByte(CP_ACP, 0, wide_path, -1, out, out_size, NULL, NULL);
        CoTaskMemFree(wide_path);
        return true;
    }

    char saved_games[MAX_PATH];
    hr = SHGetFolderPathA(NULL, CSIDL_PERSONAL, NULL, SHGFP_TYPE_CURRENT, saved_games);
    if (SUCCEEDED(hr)) {
        char* last_slash = strrchr(saved_games, '\\');
        if (last_slash) { *last_slash = '\0'; snprintf(out, out_size, "%s\\Saved Games", saved_games); return true; }
    }

    return ExpandEnvString("%USERPROFILE%\\Saved Games", out, out_size);
}

static bool TryRedirectPath(const char* original_path,
                            char* redirected_path, size_t buf_size) {
    if (!original_path || !redirected_path || buf_size == 0)
        return false;

    const char* found = NULL;
    const char* p = original_path;
    while (*p) {
        bool at_boundary = (p == original_path) ||
                           (*(p-1) == '\\') || (*(p-1) == '/') || (*(p-1) == '.');
        if (at_boundary && _strnicmp(p, "savedata", 8) == 0) {
            char next = *(p + 8);
            if (next == '\\' || next == '/' || next == '\0') { found = p; break; }
        }
        p++;
    }
    if (!found) return false;

    const char* sub_path = found + 8;
    while (*sub_path == '\\' || *sub_path == '/') sub_path++;

    size_t target_len = strlen(g_target_path);
    size_t sub_len    = strlen(sub_path);
    size_t needed     = target_len + (sub_len > 0 ? 1 : 0) + sub_len + 1;
    if (needed > buf_size) return false;

    strcpy_s(redirected_path, buf_size, g_target_path);
    if (sub_len > 0) { strcat_s(redirected_path, buf_size, "\\"); strcat_s(redirected_path, buf_size, sub_path); }
    return true;
}

// ============================================================
// Hook 函数实现
// ============================================================

static void PrepareRedirectDir(const char* redirected) {
    char dir[MAX_PATH];
    strcpy_s(dir, MAX_PATH, redirected);
    char* last_slash = strrchr(dir, '\\');
    if (last_slash) { *last_slash = '\0'; EnsureDirectoryExists(dir); }
}

static HANDLE WINAPI Hook_CreateFileA(
    LPCSTR lpFileName, DWORD dwDesiredAccess, DWORD dwShareMode,
    LPSECURITY_ATTRIBUTES lpSecurityAttributes,
    DWORD dwCreationDisposition, DWORD dwFlagsAndAttributes, HANDLE hTemplateFile)
{
    char redirected[MAX_PATH];
    if (TryRedirectPath(lpFileName, redirected, sizeof(redirected))) {
        if (dwCreationDisposition == CREATE_NEW || dwCreationDisposition == CREATE_ALWAYS ||
            dwCreationDisposition == OPEN_ALWAYS) PrepareRedirectDir(redirected);
        return g_orig_CreateFileA(redirected, dwDesiredAccess, dwShareMode,
            lpSecurityAttributes, dwCreationDisposition, dwFlagsAndAttributes, hTemplateFile);
    }
    return g_orig_CreateFileA(lpFileName, dwDesiredAccess, dwShareMode,
        lpSecurityAttributes, dwCreationDisposition, dwFlagsAndAttributes, hTemplateFile);
}

static HANDLE WINAPI Hook_CreateFileW(
    LPCWSTR lpFileName, DWORD dwDesiredAccess, DWORD dwShareMode,
    LPSECURITY_ATTRIBUTES lpSecurityAttributes,
    DWORD dwCreationDisposition, DWORD dwFlagsAndAttributes, HANDLE hTemplateFile)
{
    char ansi[MAX_PATH];
    WideCharToMultiByte(CP_ACP, 0, lpFileName, -1, ansi, MAX_PATH, NULL, NULL);
    char redirected[MAX_PATH];
    if (TryRedirectPath(ansi, redirected, sizeof(redirected))) {
        if (dwCreationDisposition == CREATE_NEW || dwCreationDisposition == CREATE_ALWAYS ||
            dwCreationDisposition == OPEN_ALWAYS) PrepareRedirectDir(redirected);
        WCHAR wide_redirected[MAX_PATH];
        MultiByteToWideChar(CP_ACP, 0, redirected, -1, wide_redirected, MAX_PATH);
        return g_orig_CreateFileW(wide_redirected, dwDesiredAccess, dwShareMode,
            lpSecurityAttributes, dwCreationDisposition, dwFlagsAndAttributes, hTemplateFile);
    }
    return g_orig_CreateFileW(lpFileName, dwDesiredAccess, dwShareMode,
        lpSecurityAttributes, dwCreationDisposition, dwFlagsAndAttributes, hTemplateFile);
}

static BOOL WINAPI Hook_CreateDirectoryA(LPCSTR lpPathName, LPSECURITY_ATTRIBUTES lpSecurityAttributes)
{
    char redirected[MAX_PATH];
    if (TryRedirectPath(lpPathName, redirected, sizeof(redirected))) {
        EnsureDirectoryExists(redirected);
        return TRUE;
    }
    return g_orig_CreateDirectoryA(lpPathName, lpSecurityAttributes);
}

static BOOL WINAPI Hook_CreateDirectoryW(LPCWSTR lpPathName, LPSECURITY_ATTRIBUTES lpSecurityAttributes)
{
    char ansi[MAX_PATH];
    WideCharToMultiByte(CP_ACP, 0, lpPathName, -1, ansi, MAX_PATH, NULL, NULL);
    char redirected[MAX_PATH];
    if (TryRedirectPath(ansi, redirected, sizeof(redirected))) {
        EnsureDirectoryExists(redirected);
        return TRUE;
    }
    return g_orig_CreateDirectoryW(lpPathName, lpSecurityAttributes);
}

static DWORD WINAPI Hook_GetFileAttributesA(LPCSTR lpFileName)
{
    char redirected[MAX_PATH];
    if (TryRedirectPath(lpFileName, redirected, sizeof(redirected)))
        return g_orig_GetFileAttributesA(redirected);
    return g_orig_GetFileAttributesA(lpFileName);
}

static DWORD WINAPI Hook_GetFileAttributesW(LPCWSTR lpFileName)
{
    char ansi[MAX_PATH];
    WideCharToMultiByte(CP_ACP, 0, lpFileName, -1, ansi, MAX_PATH, NULL, NULL);
    char redirected[MAX_PATH];
    if (TryRedirectPath(ansi, redirected, sizeof(redirected))) {
        WCHAR wide_redirected[MAX_PATH];
        MultiByteToWideChar(CP_ACP, 0, redirected, -1, wide_redirected, MAX_PATH);
        return g_orig_GetFileAttributesW(wide_redirected);
    }
    return g_orig_GetFileAttributesW(lpFileName);
}

static BOOL WINAPI Hook_DeleteFileA(LPCSTR lpFileName)
{
    char redirected[MAX_PATH];
    if (TryRedirectPath(lpFileName, redirected, sizeof(redirected)))
        return g_orig_DeleteFileA(redirected);
    return g_orig_DeleteFileA(lpFileName);
}

static BOOL WINAPI Hook_DeleteFileW(LPCWSTR lpFileName)
{
    char ansi[MAX_PATH];
    WideCharToMultiByte(CP_ACP, 0, lpFileName, -1, ansi, MAX_PATH, NULL, NULL);
    char redirected[MAX_PATH];
    if (TryRedirectPath(ansi, redirected, sizeof(redirected))) {
        WCHAR wide_redirected[MAX_PATH];
        MultiByteToWideChar(CP_ACP, 0, redirected, -1, wide_redirected, MAX_PATH);
        return g_orig_DeleteFileW(wide_redirected);
    }
    return g_orig_DeleteFileW(lpFileName);
}

static HANDLE WINAPI Hook_FindFirstFileA(LPCSTR lpFileName, LPWIN32_FIND_DATAA lpFindFileData)
{
    char redirected[MAX_PATH];
    if (TryRedirectPath(lpFileName, redirected, sizeof(redirected)))
        return g_orig_FindFirstFileA(redirected, lpFindFileData);
    return g_orig_FindFirstFileA(lpFileName, lpFindFileData);
}

static HANDLE WINAPI Hook_FindFirstFileW(LPCWSTR lpFileName, LPWIN32_FIND_DATAW lpFindFileData)
{
    char ansi[MAX_PATH];
    WideCharToMultiByte(CP_ACP, 0, lpFileName, -1, ansi, MAX_PATH, NULL, NULL);
    char redirected[MAX_PATH];
    if (TryRedirectPath(ansi, redirected, sizeof(redirected))) {
        WCHAR wide_redirected[MAX_PATH];
        MultiByteToWideChar(CP_ACP, 0, redirected, -1, wide_redirected, MAX_PATH);
        return g_orig_FindFirstFileW(wide_redirected, lpFindFileData);
    }
    return g_orig_FindFirstFileW(lpFileName, lpFindFileData);
}

// ============================================================
// Hook 安装 / 卸载
// ============================================================

#define CREATE_HOOK(name, api) do { \
    void* target = GetProcAddress(kernel32, api); \
    if (target) { \
        MH_STATUS s = MH_CreateHook(target, (void*)Hook_##name, (void**)&g_orig_##name); \
        if (s == MH_OK) { ok++; } \
        else { LogWrite("MH_CreateHook(" api ") failed: %s", MH_StatusToString(s)); fail++; } \
    } else { LogWrite("SKIP " api " (not found)"); } \
} while(0)

static bool InstallAllHooks() {
    MH_STATUS s = MH_Initialize();
    if (s != MH_OK) {
        LogWrite("MH_Initialize failed: %s", MH_StatusToString(s));
        return false;
    }

    HMODULE kernel32 = GetModuleHandleA("kernel32.dll");
    if (!kernel32) { LogWrite("Cannot get kernel32.dll"); return false; }

    int ok = 0, fail = 0;

    CREATE_HOOK(CreateFileA,        "CreateFileA");
    CREATE_HOOK(CreateFileW,        "CreateFileW");
    CREATE_HOOK(CreateDirectoryA,   "CreateDirectoryA");
    CREATE_HOOK(CreateDirectoryW,   "CreateDirectoryW");
    CREATE_HOOK(GetFileAttributesA, "GetFileAttributesA");
    CREATE_HOOK(GetFileAttributesW, "GetFileAttributesW");
    CREATE_HOOK(DeleteFileA,        "DeleteFileA");
    CREATE_HOOK(DeleteFileW,        "DeleteFileW");
    CREATE_HOOK(FindFirstFileA,     "FindFirstFileA");
    CREATE_HOOK(FindFirstFileW,     "FindFirstFileW");

    LogWrite("Hook create: %d OK, %d failed", ok, fail);
    if (fail > 0) return false;

    s = MH_EnableHook(MH_ALL_HOOKS);
    if (s != MH_OK) {
        LogWrite("MH_EnableHook failed: %s", MH_StatusToString(s));
        return false;
    }

    return true;
}

static void RemoveAllHooks() {
    if (!g_hooks_installed) return;
    MH_DisableHook(MH_ALL_HOOKS);
    MH_Uninitialize();
    g_hooks_installed = false;
    LogWrite("All hooks removed");
}

#undef CREATE_HOOK

// ============================================================
// 迁移旧的存档文件
// ============================================================

static void MigrateOldSavesIfNeeded() {
    DWORD attr = GetFileAttributesA("savedata");
    if (attr == INVALID_FILE_ATTRIBUTES || !(attr & FILE_ATTRIBUTE_DIRECTORY))
        return;

    char check_path[MAX_PATH];
    snprintf(check_path, MAX_PATH, "%s\\*", g_target_path);
    WIN32_FIND_DATAA fd;
    HANDLE hFind = FindFirstFileA(check_path, &fd);
    if (hFind != INVALID_HANDLE_VALUE) { FindClose(hFind); return; }

    LogWrite("Detected old savedata — migrating...");
    EnsureDirectoryExists(g_target_path);

    char src_search[MAX_PATH];
    snprintf(src_search, MAX_PATH, "savedata\\*");
    hFind = FindFirstFileA(src_search, &fd);
    if (hFind == INVALID_HANDLE_VALUE) { LogWrite("No files to migrate"); return; }

    int copied = 0;
    do {
        if (fd.cFileName[0] == '.') continue;
        char src[MAX_PATH], dst[MAX_PATH];
        snprintf(src, MAX_PATH, "savedata\\%s", fd.cFileName);
        snprintf(dst, MAX_PATH, "%s\\%s", g_target_path, fd.cFileName);
        if (fd.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY) {
            CreateDirectoryA(dst, NULL);
        } else {
            if (CopyFileA(src, dst, FALSE)) copied++;
        }
    } while (FindNextFileA(hFind, &fd));
    FindClose(hFind);

    LogWrite("Migration done: %d files -> \"%s\"", copied, g_target_path);
}

// ============================================================
// DllMain 入口
// ============================================================

BOOL APIENTRY DllMain(HMODULE hModule, DWORD ul_reason_for_call, LPVOID lpReserved) {
    if (ul_reason_for_call == DLL_PROCESS_ATTACH) {
        DisableThreadLibraryCalls(hModule);

        LogInit(hModule);
        LogWrite("==================================");
        LogWrite("Save Redirect ASI loading...");

        BuildIniPath(hModule);
        CreateDefaultIniIfNeeded();

        // 日志开关
        g_log_enabled = (ReadIniInt("Debug", "EnableLog", 1) != 0);

        char saved_games_path[MAX_PATH];
        if (!GetSavedGamesPath(saved_games_path, sizeof(saved_games_path)))
            ExpandEnvString("%USERPROFILE%\\Saved Games", saved_games_path, MAX_PATH);

        char default_target[MAX_PATH];
        snprintf(default_target, MAX_PATH, "%s\\OnechanbaraZ2", saved_games_path);

        char raw_config[MAX_PATH] = "";
        ReadIniString("SaveRedirect", "TargetPath", default_target, raw_config, MAX_PATH);
        if (!ExpandEnvString(raw_config, g_target_path, MAX_PATH))
            strcpy_s(g_target_path, MAX_PATH, default_target);

        LogWrite("Target save path: \"%s\"", g_target_path);
        EnsureDirectoryExists(g_target_path);

        MigrateOldSavesIfNeeded();

        if (InstallAllHooks()) {
            g_hooks_installed = true;
            LogWrite("All hooks installed — save redirection active");
        } else {
            LogWrite("ERROR: Hook installation failed!");
        }

        LogWrite("==================================");
    }
    else if (ul_reason_for_call == DLL_PROCESS_DETACH) {
        RemoveAllHooks();
    }

    return TRUE;
}
