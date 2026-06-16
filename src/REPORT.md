# 项目完结总报告：OnechanbaraZ2_Resolution / OnechanbaraZ2_SaveRedirect

> **项目周期**：2026-06-16  
> **最终状态**：100% 完成  

---

## 一、项目概述

为 PC 版《お姉チャンバラZ2 ～カオス～》开发两个 ASI 插件：
1. **自定义分辨率 + 高 DPI 支持**
2. **存档目录重定向至 `%USERPROFILE%\Saved Games\`**

全程遵守沙盒隔离原则：游戏本体只读，所有产出在 `workspace/` 下。

---

## 二、最终发行产物（根目录放置，仅需分发 .asi 单文件）

| 文件 | 大小 | 功能 |
|---|---|---|
| `OnechanbaraZ2_Resolution.asi` | 151 KB | 自定义分辨率内存补丁 + 高 DPI 感知 |
| `OnechanbaraZ2_SaveRedirect.asi` | 167 KB | 存档路径透明重定向 + 旧存档自动迁移 |

首次运行自动生成带中文注释的 `.ini` 配置文件。

---

## 三、OnechanbaraZ2_Resolution.asi 技术规格

| 项目 | 详情 |
|---|---|
| 补丁点 | **14 处 RVA**：5 渲染初始化 + 2 窗口客户区 + 2 分辨率枚举表（覆盖全屏/窗口化） |
| 高 DPI | Per-Monitor V2（三级级联回退：PerMonitorV2 → PerMonitorAware → SystemDPIAware） |
| 日志 | 每次启动自动清空旧日志，`[Debug] EnableLog=0/1` 开关 |
| 外部依赖 | **零**（仅 kernel32.lib + user32.lib） |

---

## 四、OnechanbaraZ2_SaveRedirect.asi 技术规格

| 项目 | 详情 |
|---|---|
| Hook 框架 | **MinHook**（静态编译入 .asi，无需分发额外 DLL） |
| Hook API | 10 个 kernel32 文件 API（CreateFileA/W, FindFirstFileA/W 等） |
| 重定向目标 | `%USERPROFILE%\Saved Games\OnechanbaraZ2` |
| 目标定位 | `SHGetKnownFolderPath` + 多级回退 |
| 自动迁移 | 首次运行复制旧存档到新位置 |
| 日志 | 自动清理 + INI 开关 |

---

## 五、关键技术决策与踩坑记录

| 阶段 | 问题 | 方案 | 结果 |
|---|---|---|---|
| M1 | 分辨率常量定位 | 修补 EXE 的 Hex 对比 | 精确定位 14 处 RVA |
| M2 | 存档重定向崩溃 | 自研 x64 inline hook (`x64_hook.h`) 未处理 RIP-relative 指令 | ❌ 弃用，切换 MinHook |
| M3 | MinHook 集成 | 引入 `minhook-master/` 源码编译链接 | ✅ 完美解决 |
| M3 | 窗口化分辨率不生效 | 补点不完整（缺窗口客户区 4 处 + 分辨率表 2 处） | ✅ 补全至 14 处 |
| M4 | 视频黑屏 (待确认) | 可能 Ultimate ASI Loader / Hook I/O 时序 | ⚠️ 已知限制 |

---

## 六、源码结构

```
src/
├── CMakeLists.txt
├── build.cmd
├── OnechanbaraZ2_Resolution/
│   ├── main.cpp                              ← 分辨率 ASI 主源码
│   └── OnechanbaraZ2_Resolution.ini
└── OnechanbaraZ2_SaveRedirect/
    ├── main.cpp                              ← 存档重定向 ASI 主源码
    └── x64_hook.h                            ← 已弃用的自研 hook（保留参考）
```

```
build/
├── OnechanbaraZ2_Resolution.asi              ← 最终产物 (151 KB)
└── OnechanbaraZ2_SaveRedirect.asi            ← 最终产物 (167 KB)
```

---

## 七、已知限制

1. **存档重定向**：不直接 hook CRT `fopen`，但内部调用 `CreateFileA` 可间接拦截，覆盖场景足够
2. **全屏 DPI 关闭**：可能出现黑边（GPU 缩放不匹配），建议保持 DPI 开启
3. **视频黑屏**：游戏开头动画在 Ultimate ASI Loader 环境下可能黑屏，需进一步隔离测试确认是否为 ASI 副作用
