# learn-input: Linux 输入系统深度教程

> 目标：帮助开发者快速且深入地理解 Linux（Wayland 桌面）下**输入系统**的使用、开发和调试——从内核按键一路讲到输入法引擎与词库。

## 这个教程解决什么问题

1. **全链路理解**：一次按键从硬件中断到屏幕上出现文字，中间经历了什么？配置/不配置输入法时链路有何不同？（结合 linux-kernel 源码级讲解）
2. **框架对比**：IBus 与 Fcitx5 两大输入法框架的架构、协议、生态对比，如何选型。
3. **词库内幕**：输入法词库（系统词库 / 用户词库）的存储格式、维护工具链——结合 libpinyin 与 libime 源码剖析，回答"为什么 ibus-libpinyin 的词库不好维护"。
4. **上手贡献**：如果想参与开源输入法项目（提交词库、改引擎），从哪里开始。

## 文档地图（建议按序阅读）

| # | 文档 | 内容 | 对应目标 |
|---|------|------|---------|
| 01 | [全景：一次按键的旅程](docs/01-overview.md) | 分层模型、四条典型链路、核心概念（keycode/keysym/preedit/commit） | 1 |
| 02 | [内核输入子系统](docs/02-kernel-input.md) | i8042/usbhid → input core → evdev → uinput，6.12 内核源码走读 | 1, 3 |
| 03 | [不配输入法时的链路](docs/03-userspace-no-ime.md) | libinput、XKB、wl_keyboard，事件如何到达应用 | 1, 3 |
| 04 | [Wayland 输入法协议森林](docs/04-wayland-ime-protocols.md) | text-input v1/v2/v3、input-method v1/v2、各桌面支持矩阵 | 1, 2 |
| 05 | [IBus 与 Fcitx5 架构对比](docs/05-ibus-vs-fcitx5.md) | 进程模型、DBus 协议、引擎 API、生态、选型建议 | 2 |
| 06 | [配置输入法后的完整链路](docs/06-ime-chains.md) | GTK/Qt 前端模块、DBus、XWayland/XIM、Electron、Flatpak 路径复盘 | 1, 2 |
| 07 | [词库内幕与维护](docs/07-dictionary-internals.md) | libpinyin / libime 词库格式源码解析、工具链、迁移与同步实践 | 3 |
| 08 | [调试工具箱](docs/08-debugging.md) | 按层组织：evtest → libinput → wev → ibus/fcitx5 日志，常见故障树 | 1, 2 |
| 09 | [参与开源贡献指南](docs/09-contribution.md) | 项目活跃度、代码规模实测、构建方法、上手路径与方案选择 | 4 |

另有实践性配置笔记：[linux-chinese-pinyin-vim-config.md](linux-chinese-pinyin-vim-config.md)（IBus + Vim 自动切换的入门操作手册）。

每章末尾附**动手实验**（标注 💻），所用命令均在教程环境实测。

## 教程环境快照（写作时实测）

```
系统      Debian 13 (trixie), kernel 6.12.63-amd64 (QEMU 虚拟机)
桌面      GNOME (Wayland) + Xwayland, GDM 登录
输入法    ibus 1.5.32 + ibus-libpinyin 1.15.8（桌面默认，正在运行）
          fcitx5 5.1.12 + fcitx5-chinese-addons 5.1.8（已安装，双框架共存）
输入设备  AT Translated Set 2 keyboard (event0)   ← i8042/AT 键盘
          QEMU QEMU USB Tablet (event2)            ← usbhid
          ImExPS/2 Generic Explorer Mouse (event3) ← psmouse
          xremap (event5)                          ← uinput 虚拟设备（活例子！）
```

> 本环境是 QEMU 虚拟机，正好覆盖了三类内核输入驱动（AT/PS2、USB HID、uinput），
> 且本机运行的 xremap 就是一个 uinput 虚拟键盘设备，教程多处用它做实例。

## 仓库结构

```
learn-input/
├── README.md                            # 本文件
├── linux-chinese-pinyin-vim-config.md   # 入门配置笔记（已有）
├── docs/                                # 教程正文（01–09）
├── scripts/                             # 实用脚本（词库迁移等）
└── third_party/                         # 上游源码（浅克隆，供源码走读引用）
    ├── ibus/                 # IBus 框架本体（libibus/daemon/引擎API）
    ├── libpinyin/            # 拼音引擎核心库（词库格式在这）
    ├── ibus-libpinyin/       # IBus 拼音引擎（libpinyin 的 IBus 封装）
    ├── fcitx5/               # Fcitx5 框架本体（addon 架构、各种 frontend）
    ├── fcitx5-chinese-addons/# Fcitx5 中文引擎（拼音/五笔/云拼音等）
    └── libime/               # Fcitx5 的输入法算法库（词库/解码器在这）
```

克隆时的上游 HEAD（均为 2026-09 快照，引用源码时以 `git log -1` 为准）：

| 仓库 | 用途 | 教程中主要看 |
|---|---|---|
| ibus | 输入法框架 | `src/ibusinputcontext.c`、`src/ibusengine.c`（DBus 协议面） |
| libpinyin | 拼音引擎库 | `src/storage/`（词库格式）、`src/pinyin.cpp`（用户数据生命周期） |
| ibus-libpinyin | IBus 引擎封装 | `src/PYLibPinyin.cc`（词库导入导出）、`setup/`（GUI） |
| fcitx5 | 输入法框架 | `src/frontend/`（5 种前端）、`src/lib/fcitx/`（核心抽象） |
| fcitx5-chinese-addons | 中文引擎 | `im/pinyin/pinyin.cpp`（引擎与用户词库保存） |
| libime | 算法库 | `src/libime/pinyin/pinyindictionary.*`、`tools/`（词库工具链） |

`scripts/` 目录：

| 脚本 | 用途 |
|---|---|
| `ibus2fcitx5.py` | ibus-libpinyin 导出的用户词库文本 → fcitx5/libime 文本词库格式（用法见 docs/09 第 5 节，`--test` 自测） |

## 全景图（剧透版）

```
 ┌────────────────┴─────────────── 一次按键的旅程 ───────────────┴───────────────┐

  硬件          内核空间                       用户空间
 ┌──────┐   ┌──────────────────────────┐   ┌────────────────────────────────────┐
 │ 键盘 │→IRQ│ i8042 → atkbd            │   │ ┌──────────────────────────────┐   │
 │ (USB)│───│ usbhid → hid              │   │ │ Compositor (mutter/KWin/sway)│   │
 │      │   │        ↓                  │   │ │  libinput 读 evdev           │   │
 │      │   │ Input Core (input.c)      │──→│ │  XKB: keycode→keysym→文本    │   │
 │      │   │        ↓                  │evdev│        ↓                     │   │
 │      │   │ evdev (/dev/input/eventN) │   │ │  wl_keyboard ──────────┐     │   │
 │      │   │ uinput (虚拟设备 ↩)       │   │ │  text-input-v3         │     │   │
 └──────┘   └──────────────────────────┘   │ │        ↓              ↓     │   │
                                           │ │   [无输入法]      [有输入法]  │   │
                                           │ │        ↓         IM 框架    │   │
                                           │ │     wl_seat      (ibus/fcitx5)│  │
                                           │ └─────────┬──────────┬────────┘   │
                                           └───────────┼──────────┼────────────┘
                                                       ↓          ↓
                                                   GTK/Qt 应用   候选窗/预编辑
```

细节从 [01-overview.md](docs/01-overview.md) 展开。

## 术语速查

| 术语 | 含义 |
|---|---|
| scancode | 键盘硬件扫描码（如 AT Set 2），设备相关，内核驱动负责转成 Linux keycode |
| keycode | Linux 统一键码（`KEY_A=30`），`include/uapi/linux/input-event-codes.h` 定义 |
| keysym | XKB 符号键码（`XKB_KEY_a=0x61`），keycode + 键盘布局 + 修饰键状态的结果 |
| preedit | 预编辑串——输入法组合中的文本（拼音串/候选高亮），尚未提交给应用 |
| commit | 提交——输入法把最终文本发给应用，应用收到的是"成品"字符串 |
| IME / IM | Input Method Editor / Framework，输入法（引擎）/ 输入法框架 |
| IM module | 应用工具库里对接 IM 框架的插件（GTK 的 `im-ibus.so`、Qt 的 platforminputcontext） |
| engine (IBus) / addon (Fcitx5) | 输入法引擎：拼音、五笔、rime……框架负责调度，引擎负责"拼音→汉字" |
| seat | 一组键盘+鼠标+屏幕的逻辑组合（wl_seat），Wayland 多 seat 的基础概念 |
