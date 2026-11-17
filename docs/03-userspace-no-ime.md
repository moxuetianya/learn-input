# 03 · 不配输入法时的链路：libinput → XKB → wl_keyboard → 应用

> 这是"链路 A"的展开：没有 IBus/Fcitx5，一个美式键盘敲 `a`，屏幕上如何
> 出现 `a`。搞清这条基线链路，后面加上输入法时才能明白"多出来的部分"是什么。

## 1. libinput：evdev 之上的统一抽象

### 1.1 角色

libinput（Wayland 时代事实标准，X11 也通过 xf86-input-libinput 使用）从多个
`/dev/input/eventN` 读取事件，提供：

- **逻辑设备抽象**：触摸板 = 触摸板（不是"触摸屏+鼠标"两设备）；一个
  USB 接收器的一套键鼠归为一个物理设备组（`LIBINPUT_DEVICE_GROUP`）；
- **seat 归属**：按 udev 的 `ID_SEAT` 标签分组（默认 `seat0`）；
- **指针加工**：加速度曲线、自然滚动、滚轮刻度归一化、点击模拟（tap）、
  双指手势识别……全部在 libinput 完成，compositor 拿到的是成品；
- **键盘几乎透传**：注意——**libinput 不做键表翻译**！键盘事件只透传
  keycode+状态（连自动重复都是内核做的）。为什么不透传也要过 libinput？
  为了统一设备管理与配置（输入源设置、禁用设备等）。

### 1.2 API 骨架（libinput 事件循环）

```c
libinput = libinput_udev_create_context(&interface, NULL, udev);
libinput_udev_assign_seat(libinput, "seat0");
// 集成进主循环（epoll fd）:
libinput_get_fd(libinput);
// 每次可读:
libinput_dispatch(libinput);
while ((event = libinput_get_event(libinput))) {
    switch (libinput_event_get_type(event)) {
    case LIBINPUT_EVENT_KEYBOARD_KEY:
        key  = libinput_event_keyboard_get_keycode(event);  // 就是 Linux keycode
        state= libinput_event_keyboard_get_key_state(event);
        ...
    }
    libinput_event_destroy(event);
}
```

### 1.3 mutter 中的位置（GNOME 实际路径）

```
/dev/input/eventN → libinput (libmeta 输入后端)
                  → meta-seat-native / MetaInputDeviceNative
                  → clutter/stage 事件 (ClutterKeyEvent)
                  → MetaWaylandKeyboard → wl_keyboard.key() 发给焦点应用
```

键盘设置（gnome-control-center"键盘"页）写入 gsettings，mutter 的
`meta-input-settings` 应用到 libinput；XKB 布局则在 mutter 编译（下节）。

## 2. XKB：keycode → keysym → 文字（用户态键表）

### 2.1 keymap 的五段结构

XKB keymap 是声明式"小语言"，编译产物描述完整翻译规则：

| 段 | 作用 | 例 |
|---|---|---|
| keycodes | 起别名 | `<AE01>` = 数字1那颗键 |
| compat | 修饰键语义/指示灯 | CapsLock 行为 |
| types | 组合类型 | "Shift+键 → 第二级符号" |
| symbols | 核心映射表 | `key <AE01> { [ 1, exclam ] };` |
| geometry | 物理形状（已无用） | — |

源文件在 `xkeyboard-config` 包（`/usr/share/X11/xkb/`），用户用 **RMLVO**
（Rules/Model/Layout/Variant/Options，如 `layout=us` 或 `cn` 中文布局）选择，
`xkb_keymap_new_from_names()` 按规则编译出完整 keymap。

### 2.2 状态机与翻译 API（libxkbcommon）

```c
ctx   = xkb_context_new(XKB_CONTEXT_NO_FLAGS);
keymap= xkb_keymap_new_from_names(ctx, &names, ...);   // RMLVO → keymap
state = xkb_state_new(keymap);
// 每次按键:
xkb_state_update_key(state, keycode, XKB_KEY_DOWN);
keysym = xkb_state_key_get_one_sym(state, keycode);    // e.g. XKB_KEY_a
utf32  = xkb_state_key_get_utf32(state, keycode);      // 'a' → U+0061
mods   = xkb_state_serialize_mods(state, XKB_STATE_MODS_EFFECTIVE);
```

状态 = 四元组（depressed 按住 / latched 粘住一次 / locked 锁定如 CapsLock /
group 布局组——Alt+Space 切布局就是切 group）。**这就是 01 章"键盘焦点
切换要恢复状态"的实体**。

### 2.3 Wayland 下 keymap 如何到达应用

compositor 编译好 keymap 后：

```
mutter: xkb_keymap_get_as_string() → 编译产物文本
      → wl_keyboard.keymap(format=XKB_KEYMAP_FORMAT_TEXT_V1, fd, size)
        （memfd 传递，应用 mmap）
应用:  xkb_keymap_new_from_buffer() + 自建 xkb_state
```

为什么要共享给应用？因为 `wl_keyboard.key()` 发的是**原始 keycode**（Linux
keycode，如 30），应用必须自己维护 XKB 状态才能：判断快捷键（Ctrl+S）、
处理死键组合、无焦点部件的按键。compositor 与应用的状态由 `modifiers` 事件
（四元组）和 key 事件流保持一致。

### 2.4 动手实验 💻

```bash
$ sudo apt install xkbcli libxkbcommon-tools?   # 包名 xkbcli
$ xkbcli how-to-type --keysym a          # 反查: 哪颗键+什么修饰出 'a'
$ xkbcli compile-keymap --layout us      # 看 RMLVO 编译出的 keymap 文本
$ xkbcli interactive-evdev               # 实时: 按键→keycode/keysym/utf8
# GNOME 当前会话的布局来源（gsettings，不是 localectl！）:
$ gsettings get org.gnome.desktop.input-sources sources
```

> 注意：Wayland 会话下 `localectl set-keymap` 只影响 VT/X11，GNOME 布局由
> gsettings `input-sources` 管理（mutter 读取）。这是常见配置踩坑点。

## 3. Wayland 核心输入协议（wl_seat 家族）

`wayland.xml` 中输入相关接口（这是所有 compositor 都实现的标准部分）：

### 3.1 wl_seat

```xml
<interface name="wl_seat" version="9">
  <event name="capabilities">  <!-- 告知有 pointer/keyboard/touch -->
  <event name="name">
</interface>
```

应用 `wl_seat.get_pointer/get_keyboard/get_touch` 拿各设备对象。

### 3.2 wl_keyboard 事件流（本教程主角）

| 事件 | 参数 | 说明 |
|---|---|---|
| `keymap` | format, fd, size | 共享 XKB keymap（一次性） |
| `enter` | serial, surface, keys[] | **获得键盘焦点**；keys=当前按住的键位图 |
| `key` | serial, time, key, state | key=**Linux keycode**；state=down/up |
| `modifiers` | serial, 四元组 | 修饰键状态同步 |
| `leave` | serial, surface | 失焦 |
| `repeat_info` | rate, delay | 重复参数（应用自行模拟重复或交给工具库） |

`serial` 是每 seat 单调递增的序号，用于把后续**请求**（如弹出菜单 grab、
粘贴选择）与**事件**配对——compositor 验证"你是真的刚收到过这个事件的客户"。

一个重要推论：**Wayland 下应用拿不到全局按键**（只收得到焦点事件），也无
法知道其他应用的存在——这是相对 X11 的安全模型改变，也是全局快捷键、
按键记录类工具在 Wayland 下必须换机制（全局快捷键协议 / XDG desktop
portal）的原因。

### 3.3 wl_pointer（顺带）

`enter/leave/motion/button/axis(+frame/axis_source/axis_stop/axis_value120)`。
v5 起用 `frame` 把一批 motion/axis 圈成帧；v8 的 `axis_value120` 让高分辨率
滚轮报告 120 为一格（取代旧的离散值猜测）。相比键盘，指针链路里 libinput
已经做了大部分加工，compositor 主要做光标管理+命中测试+转发。

## 4. 应用侧：GTK/Qt 如何消化 keycode

以 GTK4 为例（Qt6 同构）：

```
GDK Wayland 后端:
  wl_keyboard.key(30) → xkb_state → 产生 GdkKeyEvent(keyval=XKB_KEY_a)
GTK 主循环 → 分发到焦点 widget
  ├─ 先问 GtkIMContext（输入法上下文，第 06 章主角）:
  │    未配置 IM 时 → GtkIMContextSimple: 处理死键/Compose 序列
  └─ IM 不消费 → 按 keyval+mods 判断快捷键 → 否则 commit 文本 'a'
```

`GtkIMContext` 是应用与"文字产生器"之间的接口（`filter_keypress` /
`preedit-start/changed` / `commit` 信号）。**没配输入法时这个接口依然存在**，
只是背后实现是 Simple（空操作+死键）。06 章将看到 ibus/fcitx 如何替换它。

## 5. 基线链路总账

```
[按下 a 键，无输入法，GNOME Wayland]

IRQ → atkbd(1c→KEY_A) → input core → evdev → libinput(透传) → mutter
  ├─ XKB state 更新 → mutter 自身消费(全局快捷键? 无) 
  └─ wl_keyboard.key(30) ──────────────────→ GTK 应用
        xkb_state_key_get_utf32(30)='a'
        → GtkIMContextSimple(不消费) → 光标处插入 'a'
```

对照实验建议 💻：安装 `wev`（Wayland event viewer）后运行，把鼠标停在其
窗口按 `Shift+a`：

```
$ sudo apt install wev && wev
[14, wl_keyboard] key: serial: 41, time ..., key 42 (LeftShift), state 1
[14, wl_keyboard] modifiers: serial ..., depressed: 00000001 (Shift)...
[14, wl_keyboard] key: serial: 43, ..., key 30 (A), state 1
[14, wl_keyboard] key: serial: 44, ..., key 30 (A), state 0
```

可以看到：修饰键与字母键都是独立 key 事件，keysym 级的"Shift+a=A"翻译
完全发生在 wev 自己（用共享 keymap 编译的状态机）里。

## 6. X11 老路径速览（对照组）

X11 下没有 compositor 中枢，X server 兼任：`evdev→(server内)XKB→XKeyEvent`
（X keycode≠Linux keycode，有 +8 偏移的历史包袱），应用经 XIM 或 IM module
对接输入法。Xwayland 本质是一个"把 X11 应用翻译成 Wayland 客户端"的桥
（06 章链路 C）。理解 Wayland 改动动机：**把 X server 的上帝权限拆掉**，
输入焦点、键表、截屏、全局快捷键全部收归 compositor 权衡管理。

## 7. 本章小结

- libinput 统一设备抽象与指针加工；键盘事件仅透传 keycode；
- XKB（用户态）负责 keycode→keysym→文字，keymap 由 compositor 编译并
  memfd 共享给应用，双方各自维护状态机；
- `wl_keyboard` 发原始 keycode + 修饰键四元组，serial 配对事件与请求；
- 应用侧 GtkIMContext 接口永远在，"没有输入法"只是它的实现是 Simple；
- GNOME 的键盘布局在 gsettings，`localectl` 管不到 Wayland 会话；
- 全景一句话：libinput 只活在 compositor、libxkbcommon 只活在应用——
  两级翻译各管一半，跨 Wayland socket 的契约只有 keycode、keymap 与
  （有 IME 时的）成品字符串。这条分界线在输入法介入后于何处断开，
  见 04 章 §7"分野"。

下一章是本教程的协议深水区：[04-wayland-ime-protocols.md](04-wayland-ime-protocols.md)
