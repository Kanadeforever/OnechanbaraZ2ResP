/**
 * x64_hook.h — 自包含的 x64 轻量级 Inline Hook 库
 *
 * 基于绝对跳转 (FF 25 + 8-byte addr)，零外部依赖。
 * 自动分配 Trampoline 以确保 Hook 函数可安全调用原始函数。
 *
 * 用法:
 *   void* trampoline = NULL;
 *   X64Hook_Install(TargetFunc, MyHook, &trampoline);
 *   // 通过 trampoline 调用原始函数
 *   X64Hook_Remove(TargetFunc, trampoline);  // 自动释放 trampoline
 */

#pragma once
#include <windows.h>
#include <stdint.h>

#define X64_JMP_SIZE 14
#define X64_TRAMPOLINE_SIZE (X64_JMP_SIZE * 2)  // original bytes + jump back

#pragma pack(push, 1)
struct X64JmpInstruction {
    uint8_t  opcode[2];   // FF 25
    uint32_t disp;        // 00 00 00 00
    uint64_t address;     // absolute target
};
#pragma pack(pop)

/**
 * 安装 Hook — 同时分配 Trampoline
 *
 * @param target_func     要被 Hook 的函数地址
 * @param hook_func       我们的 Hook 函数地址
 * @param trampoline_out  输出：分配的可执行 Trampoline 地址
 *                        （调用原始函数时使用此地址，而非 target_func）
 * @return true 如果成功
 */
static bool X64Hook_Install(void* target_func, void* hook_func, void** trampoline_out) {
    if (!target_func || !hook_func || !trampoline_out) {
        return false;
    }

    *trampoline_out = NULL;

    // 1. 保存目标函数的前 14 字节
    uint8_t original_bytes[X64_JMP_SIZE];
    memcpy(original_bytes, target_func, X64_JMP_SIZE);

    // 2. 分配可执行内存作为 Trampoline
    //    布局: [原始 14 字节] + [JMP 跳回 target_func+14]
    uint8_t* trampoline = (uint8_t*)VirtualAlloc(
        NULL, X64_TRAMPOLINE_SIZE,
        MEM_COMMIT | MEM_RESERVE,
        PAGE_EXECUTE_READWRITE
    );

    if (!trampoline) {
        return false;
    }

    // 3. 写入 Trampoline：原始字节 + 跳回
    memcpy(trampoline, original_bytes, X64_JMP_SIZE);

    X64JmpInstruction jmp_back;
    jmp_back.opcode[0] = 0xFF;
    jmp_back.opcode[1] = 0x25;
    jmp_back.disp      = 0x00000000;
    jmp_back.address   = (uint64_t)((uint8_t*)target_func + X64_JMP_SIZE);
    memcpy(trampoline + X64_JMP_SIZE, &jmp_back, X64_JMP_SIZE);

    // 4. 构造跳转到 Hook 的指令
    X64JmpInstruction jmp_to_hook;
    jmp_to_hook.opcode[0] = 0xFF;
    jmp_to_hook.opcode[1] = 0x25;
    jmp_to_hook.disp      = 0x00000000;
    jmp_to_hook.address   = (uint64_t)hook_func;

    // 5. 修改目标函数内存保护并写入 JMP
    DWORD old_protect;
    if (!VirtualProtect(target_func, X64_JMP_SIZE, PAGE_EXECUTE_READWRITE, &old_protect)) {
        VirtualFree(trampoline, 0, MEM_RELEASE);
        return false;
    }

    memcpy(target_func, &jmp_to_hook, X64_JMP_SIZE);

    DWORD unused;
    VirtualProtect(target_func, X64_JMP_SIZE, old_protect, &unused);

    // 6. 刷新指令缓存（防止 CPU 执行过期的缓存指令）
    FlushInstructionCache(GetCurrentProcess(), target_func, X64_JMP_SIZE);
    FlushInstructionCache(GetCurrentProcess(), trampoline, X64_TRAMPOLINE_SIZE);

    *trampoline_out = trampoline;
    return true;
}

/**
 * 移除 Hook — 恢复原始字节并释放 Trampoline
 *
 * @param target_func  被 Hook 的函数地址
 * @param trampoline   之前 X64Hook_Install 返回的 Trampoline 地址
 */
static void X64Hook_Remove(void* target_func, void* trampoline) {
    if (!target_func) return;

    if (trampoline) {
        // 从 Trampoline 中读取原始字节并恢复
        uint8_t original_bytes[X64_JMP_SIZE];
        memcpy(original_bytes, trampoline, X64_JMP_SIZE);

        DWORD old_protect;
        if (VirtualProtect(target_func, X64_JMP_SIZE, PAGE_EXECUTE_READWRITE, &old_protect)) {
            memcpy(target_func, original_bytes, X64_JMP_SIZE);
            DWORD unused;
            VirtualProtect(target_func, X64_JMP_SIZE, old_protect, &unused);
            FlushInstructionCache(GetCurrentProcess(), target_func, X64_JMP_SIZE);
        }

        VirtualFree(trampoline, 0, MEM_RELEASE);
    }
}
