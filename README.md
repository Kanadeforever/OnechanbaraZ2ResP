# Onechanbara Z2 高分辨率补丁+

两个 ASI 插件，配合 Ultimate ASI Loader 使用，修复和增强 PC 版《[お姉チャンバラZ2 ～カオス～](https://store.steampowered.com/app/446390)》。

## 插件

| 插件 | 功能 |
|---|---|
| `OnechanbaraZ2_Resolution.asi` | 自定义渲染分辨率 + 高 DPI 感知 |
| `OnechanbaraZ2_SaveRedirect.asi` | 存档目录重定向到 `%USERPROFILE%\Saved Games\` |

首次运行自动生成带注释的 `.ini` 配置文件。

## 编译

```bash
mkdir build && cd build
cmake .. -G "Visual Studio 17 2022" -A x64
cmake --build . --config Release
```

MinHook 通过 CMake `FetchContent` 自动下载，无需手动处理。

## 使用方法

将 `.asi` 文件放入游戏根目录，配合 [Ultimate ASI Loader](https://github.com/ThirteenAG/Ultimate-ASI-Loader) 使用。

## 依赖

- 存档重定向需要 [MinHook](https://github.com/TsudaKageyu/minhook)
- 原始高分辨率补丁：[Guzz](https://web.archive.org/web/20190531122331/https://forums.guru3d.com/threads/nvidia-anti-aliasing-guide-updated.357956/page-184#post-5381043). 感谢他的贡献不然不能成功的转换成不需要修改exe的插件形式。
