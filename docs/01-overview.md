# 01 · 全景：一次按键的旅程

> 本章建立整个教程的骨架：分层模型、几条典型链路、以及贯穿全文的核心概念。
> 后续每一章都是对这里某一层的展开。

## 1. 为什么"输入"在 Linux 上这么复杂

在 Windows/macOS 上，输入法是操作系统的一部分，链路是封闭而统一的。Linux 不是：

1. **内核只负责"报告事件"**，不理解"文字"。内核给你的只有 `KEY_LEFTCTRL按下`、`坐标移动`这样的原始事实。
2. **翻译成什么文字取决于用户态**：键盘布局（US/Dvorak/中文注音…）、输入法（拼音/五笔/日文…）都是策略，放在用户态。
3. **显示协议有两套**（X11 / Wayland），Wayland 下 compositor 成为输入的必经之路，且输入法协议尚未完全标准化（见 04 章）。
4. **应用工具链多套**：GTK / Qt / Electron / 终端模拟器……每套对输入法的对接方式都略有不同。
5. **两个并存的输入法框架**（IBus / Fcitx5），各自有不同的进程模型和协议。

所以一次按键的旅程 = **内核（报告）→ 翻译层（keycode→keysym→文字）→ 可选的输入法层（组合→提交）→ 应用**，而每一层都有多个实现可选。理解了这条链，所有"输入法不工作"的问题都变成了"链路在哪一层断了"。

## 2. 分层模型

```
 Layer 0  硬件         物理键盘/鼠标，产生扫描码(scancode)或 HID 报告
 ─────────────────────────────────────────────────────────────
 Layer 1  内核驱动     atkbd (AT/PS2), usbhid/hid (USB/BT),
                        hid-multitouch, xremap 用的 uinput…
                        职责: 把设备相关编码统一为 Linux keycode
 ─────────────────────────────────────────────────────────────
 Layer 2  Input Core   drivers/input/input.c
                        input_dev 注册、事件分发、/dev/input/ 设备节点
 ─────────────────────────────────────────────────────────────
 Layer 3  evdev        /dev/input/eventN 字符设备
                        用户态通过 read() 拿到 struct input_event
 ─────────────────────────────────────────────────────────────
 Layer 4  输入栈       libinput: 读 evdev，统一设备抽象(seat)、指针加速、手势
                        （X11 时代还有 evdev/xf86-input 驱动，现已归一）
 ─────────────────────────────────────────────────────────────
 Layer 5  Compositor   mutter(GNOME)/KWin(KDE)/sway…
                        用 XKB 把 keycode 翻译成 keysym/UTF-32
                        决定键盘焦点，通过 Wayland 协议发给应用
 ─────────────────────────────────────────────────────────────
 Layer 6  输入法       IBus / Fcitx5（可选层）
                        拦截按键 → 拼音组合 → 候选 → 提交文本
 ─────────────────────────────────────────────────────────────
 Layer 7  应用         GTK/Qt/Electron/终端… 的文本控件
```

关键认知：

- **Layer 1–3 在内核**，输出的是与布局无关的 keycode 事件流；
- **Layer 5 是 Wayland 时代的枢纽**：所有输入必经 compositor（X11 时代是 X server）；
- **Layer 6 是可插拔的**：不装输入法，按键直接走 5→7；装了，5→6→7 或 7→6→7（取决于谁做 IM 客户端，见 04/06 章）。

## 3. 四条典型链路（本教程会全部走一遍）

### 链路 A：不配置输入法（纯英文/直接输入）

```
键盘 → atkbd/usbhid → input core → evdev → libinput → mutter
     → XKB 翻译 (keycode→keysym→U+0041 'a')
     → wl_keyboard.key(keycode) + 应用侧自行 XKB 翻译
     → GTK 光标处插入 'a'
```

注意一个细节：Wayland 下 **compositor 和应用各维护一份 XKB 状态**。compositor
把原始 keycode 发给应用，同时把 keymap（编译好的）通过 `wl_keyboard.keymap`
事件共享给应用，应用自己算出字符。这是为了支持应用侧快捷键判断。

### 链路 B：GTK/Qt 应用 + 输入法（主链路，06 章细讲）

```
键盘 → … → mutter → wl_keyboard.key(30)
     → GTK IM 模块 (GtkIMContext: wayland text-input 或 ibus/fcitx)
     → DBus / Wayland IM 协议 → 输入法框架 → 拼音引擎(词库查询)
     → preedit_string("ni") → 应用绘制预编辑串
     → 用户选词 → commit_string("你") → 应用插入"你"
```

### 链路 C：XWayland 应用（老 X11 应用）+ XIM

```
键盘 → … → mutter → Xwayland (作为 Wayland 客户端收键)
     → X11 事件 (XKeyEvent, keycode=X11编码) → XIM 协议 (ibus-x11/fcitx5 xim)
     → 输入法 → commit → XSendInputString 合成 KeyPress 插入文本
```

### 链路 D：文本控制台（tty，无图形）

```
键盘 → atkbd → input core → tty/VT 子系统 (drivers/tty/vt)
     → 内核内置键表翻译 (loadkeys 加载的 keymap，内核态完成翻译!)
     → tty 缓冲 → 应用 read()
```

链路 D 是唯一**翻译发生在内核里**的路径——对照组，帮助我们理解为什么图形
时代翻译被搬到了用户态（内核无法承载复杂布局/输入法策略，也无法热更新）。

## 4. 核心概念精讲

### 4.1 scancode → keycode → keysym → 文字

以按下 `A` 键（美式布局）为例，编码在三层数据结构里逐步翻译：

```
 设备层        内核层                     用户态 XKB
 scancode     keycode                    keysym        UTF-32
 (Set2:0x1C)  (KEY_A=30)                 (0x61 'a')    U+0061 'a'
     atkbd.c 查表翻译            xkb_state_key_get_one_sym()
                                  keymap = keycodes+compat+types+symbols
```

- **scancode**：设备相关。AT 键盘是 Set 2 扫描码，USB 键盘是 HID Usage
  （`usage page 0x07`），两者编码完全不同——这正是内核驱动层存在的意义。
- **keycode**：Linux 统一抽象，内核头文件
  `include/uapi/linux/input-event-codes.h` 是权威定义。**内核不做布局翻译**，
  `KEY_A` 的含义是"标着 A 的那颗键被按下"（其实严格说是"美式布局下 A 位置的键"）。
- **keysym**：XKB（用户态库 libxkbcommon）根据 keymap + 修饰键状态算出。
  同一个 keycode 30 在 Dvorak 布局下得到 `a` 以外的符号；按住 Shift 得 `A`。
- **文字**：keysym 映射到 Unicode（`xkb_keysym_to_utf32`），进应用。

输入法的介入点就在 keysym 之后：应用/框架发现这是字母 `n`、当前处于中文模式，
于是不直接上屏，而是交给输入法组合。

### 4.2 输入法的工作模型：preedit / commit / candidate

输入法本质是一个**异步的状态机**，与输入框之间只有三种核心交互：

| 概念 | 方向 | 含义 | 例子 |
|---|---|---|---|
| 按键事件 | 应用→IM | 待组合的原始输入 | `n` `i` `h` `a` `o` |
| preedit | IM→应用 | 组合中的文本，带样式，**不算正式内容** | `你好` 下方虚线 |
| commit | IM→应用 | 最终文本，一次性替换 preedit | 提交 `你好` |
| candidate | IM→面板 | 候选列表（由 IM 的候选窗渲染） | 1.你好 2.你号 3.尼豪 |
| surrounding | 应用→IM | 光标周围上下文（可选，提升整句转换质量） | 前一句 |

这组交互在不同协议里有不同表达（Wayland text-input、IBus DBus API、Fcitx5
DBus/waylandim API），但语义同源——04–06 章会反复看到它们的影子。

### 4.3 焦点与 grab（为什么快捷键会"打架"）

- **键盘焦点（keyboard focus）**：Wayland 的 `wl_keyboard.enter/leave`。一个
  seat 同一时刻只有一个键盘焦点窗口。compositor 由此决定按键发给谁。
- **compositor 快捷键 grab**：mutter/KWin 自己先消化全局快捷键（Super、
  输入法切换键），消化不了才下发。所以"输入法切换键被 GNOME 占用"发生在这一层。
- **evdev grab（EVIOCGRAB）**：更底层的独占，xremap 等工具用来防止原始按键
  泄漏（见 02 章 uinput 一节）。
- **应用内快捷键**：应用收到的还是 keycode（不是文字），所以 `Ctrl+S` 判断
  用 keycode+modifier，与 IME 无关；但 IME 激活时字母键被 IM 吃掉，这就是
  "Vim 里必须先退出 IM 才能按 jkl"的根因——也是 vim 集成配置
  （见根目录笔记）自动切 IM 的动机。

### 4.4 seat：输入设备的逻辑分组

`wl_seat` 表示"一套键盘+指针+触摸"，多 seat 意味着多套独立焦点（罕见但存在：
KVM 场景、多用户同屏）。libinput 的 `LIBINPUT_SEAT` udev 标签决定设备归属。
调试输入问题时要先确认设备在预期的 seat 里（08 章 `libinput list-devices`）。

## 5. 用本机设备感知分层（预习实验 💻）

教程环境是 QEMU 虚拟机，三种内核驱动齐全：

```bash
$ cat /proc/bus/input/devices | grep -E "^N|^H|^P"    # 设备名/处理器/节点
N: Name="AT Translated Set 2 keyboard"
P: Phys=isa0060/serio0/input0          ← i8042 串行口 → atkbd
H: Handlers=sysrq kbd leds event0

N: Name="QEMU QEMU USB Tablet"
P: Phys=usb-0000:02:01.0-1/input0      ← USB 控制器 → usbhid
H: Handlers=mouse0 event2

N: Name="xremap"
H: Handlers=sysrq kbd mouse2 event5 rfkill   ← uinput 虚拟设备！
```

解读：

- `Phys=isa0060/serio0` 表明走 i8042（PS/2 控制器）老式串行路径；
- `usb-0000:02:01.0-1` 表明走 USB 总线；
- `xremap` 没有 `Phys`（或为空）——它由 `/dev/uinput` 创建（02 章拆解）；
- `Handlers=sysrq kbd leds` 中的 `kbd` 表示该设备**同时接入了内核控制台**
  （VT 键盘路径，链路 D），`leds` 表示它有 LED（大小写灯）。

每一层都有对应的观测点（02 章开始逐层动手）。

## 6. 本章小结

- 输入链 = 内核报告事件 + 用户态翻译 + 可选 IM 组合提交；
- keycode 是内核与用户态的契约，keysym/文字是用户态策略；
- IM 的语义核心是 preedit/commit/candidate，各协议只是载体不同；
- 本机三种输入设备驱动 + xremap 虚拟设备是绝佳的活教材。

下一章进入内核：[02-kernel-input.md](02-kernel-input.md)
