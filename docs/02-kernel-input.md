# 02 · 内核输入子系统：从 IRQ 到 /dev/input/eventN

> 内核输入子系统的代码集中在 `drivers/input/`，公共 API 在
> `include/linux/input.h`（内核侧）与 `include/uapi/linux/input-event-codes.h`
> （用户态契约）。本章按"一个按键穿过内核"的时间顺序走读，内核版本 6.12
> （与教程环境 6.12.63 一致；这些核心文件近十年非常稳定）。

## 1. 三个角色：device / handler / handle

内核输入子系统是一套**发布-订阅**模型，三个数据结构构成三角：

```
 input_dev          input_handler              input_handle
 (设备:是事件源)     (消费者:谁要用事件)          (连接:dev↔handler 的边)
 drivers/input/      drivers/input/evdev.c       两者间的绑定
 keyboard/atkbd.c    drivers/tty/vt (kbd)        input_attach_handler()
 usbhid, hid...      joydev, mousedev(已弃用)
```

- 设备驱动注册 `input_register_device()`；
- handler 注册 `input_register_handler()`，通过 `id_table`/`match` 决定要不要
  认领某设备（evdev 认领一切：`.id_table = { .flags = INPUT_DEVICE_ID_MATCH_FFBIT...}`
  实际上 evdev 匹配所有设备）；
- 每条边是一个 `input_handle`，事件发生时 `input_pass_values()` 沿着边广播。

一个物理键盘同时连接到 **evdev**（给图形栈/用户态程序）和 **kbd**（给 VT
控制台），这就是 `/proc/bus/input/devices` 里 `Handlers=sysrq kbd leds event0`
的含义——一颗按键在内核里被复制成两路消费者。

## 2. 第一跳：设备驱动（scancode → keycode）

### 2.1 AT/PS2 键盘：i8042 + atkbd（本机 event0）

老式 PC 键盘走 i8042 控制器（IRQ1 键盘 / IRQ12 鼠标），路径：

```
IRQ1 → i8042_interrupt()            drivers/input/serio/i8042.c
     → serio_interrupt()            drivers/input/serio/serio.c (串行层)
     → atkbd_interrupt()            drivers/input/keyboard/atkbd.c
```

`atkbd_interrupt()` 做三件事（对照源码结构）：

1. **拼包**：AT Set 2 扫描码有扩展前缀（`0xE0`）和多字节序列，驱动把它们
   组装成完整 scancode（如 `0xE0 0x1D` → 右Ctrl，内核记作 `0x1D | 0x80`）；
2. **查表翻译**：`atkbd->keycode[]` 数组把 scancode 翻成 Linux keycode。
   这个表就是"AT Set 2 → Linux keycode"的字典，可以通过 `UI_SET_KEYBIT`
   类似的 `EVIOCSKEYCODE` ioctl 部分改写（笔记本 Fn 组合键常用）；
3. **上报**：`input_event(atskbd->dev, EV_KEY, keycode, value)`，
   value=1 按下 / 0 释放 / 2 自动重复。

可以运行时查看/修改这张表（实验 💻）：

```bash
# atkbd 的 scancode→keycode 表暴露在 sysfs
$ ls /sys/devices/platform/i8042/serio0/input/input0/
capabilities/  id/  name  properties/  uniq  ...
# 教程环境路径可能不同，用 udevadm 反查:
$ udevadm info -a -n /dev/input/event0 | grep -E "drivers|kernel" | head
```

### 2.2 USB 键盘：usbhid / hid（本机 event2 是 USB Tablet）

USB 键盘的编码是 **HID Usage**（Usage Page 0x07，`a`=0x04，`左Shift`=0xE1）：

```
USB 传输 → usbhid_irq()             drivers/hid/usbhid/hid-core.c
        → hid_input_report()        drivers/hid/hid-core.c
        → hidinput_hid_event()      drivers/hid/hid-input.c
          (usage → keycode, hid-input.c 内置映射表)
        → input_report_key()
```

HID 层还有报告描述符解析、`hid-multitouch`（触摸屏归并触点）、 quirks（某些
键盘需要 `KEYBOARD_KEY_*` 修复映射，通过 udev hwdb 配置——发行版常见）。

### 2.3 uinput：用户态造设备（本机 event5 = xremap）

`drivers/input/misc/uinput.c` 提供反向能力：用户态程序写 `/dev/uinput` 创建
一个**内核认得的虚拟输入设备**。这正是本机运行的 xremap 的原理：

```
xremap 进程:
  1. open("/dev/uinput")
  2. ioctl(UI_SET_EVBIT, EV_KEY); ioctl(UI_SET_KEYBIT, KEY_J) ... 声明能力
  3. ioctl(UI_DEV_CREATE)          ← 此刻 /dev/input/event5 诞生
  4. 同时对真实键盘 event0 做 EVIOCGRAB(独占读取)
  5. read 真实按键 → 按配置改键 → write 到 /dev/uinput 注入改后的事件
  → libinput/compositor 只看到 event5，完全不知道 xremap 存在
```

最小可复现例子（Python + evdev 库即可，无需写 C）：

```python
# pip install evdev；本机实测可创建一个只会按 KEY_A 的虚拟设备
import evdev, time
ui = evdev.UInput({evdev.ecodes.EV_KEY: [evdev.ecodes.KEY_A]}, name="my-vkbd")
ui.write(evdev.ecodes.EV_KEY, evdev.ecodes.KEY_A, 1); ui.syn()
time.sleep(0.1)
ui.write(evdev.ecodes.EV_KEY, evdev.ecodes.KEY_A, 0); ui.syn()
ui.close()
```

**认知结论**：内核不知道也不关心"谁是真键盘"。对内核而言 uinput 设备与
物理设备完全同权——这是 Linux 输入栈可组合性的根源（同族工具：xdotool 的
XTest、wtype/ydotool 的 uinput、Barrier 输入共享）。

## 3. Input Core：事件的定义与分发

### 3.1 事件的唯一格式（用户态契约）

```c
// include/uapi/linux/input.h —— 用户态 read(/dev/input/eventN) 得到的就是它
struct input_event {
    struct input_timeval time;   // 事件时间戳（monotonic）
    __u16 type;                  // EV_KEY / EV_SYN / EV_REL / EV_ABS ...
    __u16 code;                  // KEY_A / SYN_REPORT / REL_X ...
    __s32 value;                 // 1/0/2(按下/释放/重复) 或坐标增量
};
```

关键 type（完整列表在 `input-event-codes.h`）：

| type | 含义 | 典型 code |
|---|---|---|
| `EV_KEY` | 按键/按钮 | `KEY_A(30)`, `KEY_LEFTSHIFT(42)`, `BTN_LEFT(0x110)` |
| `EV_REL` | 相对移动 | `REL_X`, `REL_WHEEL` |
| `EV_ABS` | 绝对坐标 | `ABS_X`, `ABS_MT_POSITION_X` |
| `EV_SYN` | 同步标记 | `SYN_REPORT`(一包事件结束), `SYN_MT_REPORT` |
| `EV_MSC` | 杂项 | `MSC_SCAN`(原始 scancode 伴随上报!) |
| `EV_LED`, `EV_SND`, `EV_SW` | 输出侧 | 大小写灯、蜂鸣、盖子开关 |

两个容易忽略的要点：

- **`MSC_SCAN`**：atkbd/usbhid 上报 `EV_KEY` 时会附带一条 `EV_MSC/MSC_SCAN`
  携带原始 scancode/HID usage——调试键映射问题时非常有用（evtest 可见）；
- **事件以 `SYN_REPORT` 分包**：一次 `input_sync()` 之前的所有事件属于同一
  个逻辑包（比如鼠标的 X+Y 移动），这是 evdev/lininput 解析的边界。

### 3.2 上报 API 与自动重复

```c
// include/linux/input.h —— 内核侧API(设备驱动调用)
input_report_key(dev, KEY_A, 1);   // → input_event(dev, EV_KEY, KEY_A, 1)
input_sync(dev);                    // → input_event(dev, EV_SYN, SYN_REPORT, 0)
```

**按键自动重复（auto-repeat）由内核完成**：`input.c` 里每个 input_dev 有
`timer`（`input_repeat_key`），用 `input_set_capability`/`rep` 参数控制
`REP_DELAY/REP_PERIOD`（默认 250ms/33ms）。所以 evtest 里会看到
`value=2` 的事件流——用户态无需自己实现重复（但 Wayland 的
`wl_keyboard.repeat_info` 允许 compositor 覆盖参数，见 03 章）。

### 3.3 分发路径

```
input_event()  (drivers/input/input.c)
  → input_handle_event() / input_repeat_key()
  → input_pass_values(dev, vals, count)
      for 每个 handle:
        handler->events(handle, ...)      ← evdev_events() 即在此被调
```

evdev 之后把值写入每个打开者的环形缓冲并唤醒（下一节）。同时另一路
`kbd handler`（VT 控制台）也在这里收到事件——第 6 节展开。

## 4. evdev：用户态的窗口

### 4.1 设备节点与打开模型

`drivers/input/evdev.c` 为每个 input_dev 创建 `/dev/input/eventN`（N 按
注册序，udev 另提供稳定路径 `/dev/input/by-id/`、`by-path/`）。多进程可同时
打开同一设备，**各自独立环形缓冲**（`evdev_client`，默认 64 包，
`EVIOCSMASK`? 实际大小是 `EVDEV_BUFFER_SIZE`，busy polling 支持另说）。
读端 `read()` 阻塞在 waitqueue 上，`O_NONBLOCK` 时返回 `EAGAIN`——libinput
的事件循环就是 epoll 若干 eventN。

### 4.2 常用 ioctl（调试必备）

| ioctl | 作用 |
|---|---|
| `EVIOCGBIT(ev, len)` | 查询设备支持哪些 code（能力探测） |
| `EVIOCGKEY(len)` | 当前按键状态位图（哪些键正被按住） |
| `EVIOCGNAME/EVIOCGPHYS/EVIOCGID` | 设备名/物理路径/厂商ID |
| `EVIOCGRAB` / `EVIOCREVOKE` | 独占设备 / 强制踢掉其他打开者 |
| `EVIOCSKEYCODE` | 改 scancode→keycode 映射（atkbd 支持） |

`EVIOCGRAB` 值得记住： grabbing 之后事件只发给 grabber，xremap、ibus
的键盘监测、游戏反作弊都靠它。它也是"某些工具运行时 evtest 看不到事件"
这类怪象的常见原因。

### 4.3 观察一次按键（实验 💻）

```bash
$ sudo apt install evtest           # 教程环境默认未装
$ sudo evtest /dev/input/event0     # AT 键盘
# 按下 a 再松开，得到（节选）:
Event: time ..., type 4 (EV_MSC), code 4 (MSC_SCAN), value 1c   ← 原始Set2码
Event: time ..., type 1 (EV_KEY), code 30 (KEY_A), value 1      ← 按下
Event: time ..., type 4 (EV_MSC), code 4 (MSC_SCAN), value 1c
Event: time ..., type 1 (EV_KEY), code 30 (KEY_A), value 2      ← 内核自动重复
Event: time ..., type 1 (EV_KEY), code 30 (KEY_A), value 0      ← 释放
Event: time ..., type 0 (EV_SYN), code 0 (SYN_REPORT)           ← 包结束
```

对照 usb 设备：`sudo evtest /dev/input/event2` 看到的 MSC_SCAN 是 HID usage
（a 键是 `70004`——usage page 0x07 << 16 | 0x04）。

> evtest 不在时可用低配替代：`sudo cat /dev/input/event0 | xxd`（二进制
> struct input_event，小端，每条 24 字节：8+2+2+4）。

## 5. keycode 之后：内核刻意止步的地方

内核拿到了 `KEY_A`，但它**不知道也不应该知道**：

- 用户用的是美式布局还是 Dvorak（同一颗键不同符号）；
- 按 Shift+a 应该出 `A` 还是别的；
- 这颗键该不该被输入法吃掉。

所以内核的输出止于 keycode+修饰键状态（修饰键也是普通 EV_KEY，`KEY_LEFTSHIFT`
等，"是不是修饰键"是用户态 XKB 的判断）。唯一例外：VT 控制台——下一节。

## 6. 对照组：VT 控制台路径（翻译在内核里！）

`drivers/tty/vt/keyboard.c` 是活化石：为了在无用户态图形栈时也能打字，
VT 子系统把"keycode → 字符"的翻译放在了内核：

```
input_pass_values → kbd handler (drivers/tty/vt/keyboard.c)
  → kbd_keycode()
      查当前控制台的 kbd 键表 (struct kbd_table, 每 VT 一份)
      键表项是 K_* 动作: K_LATIN('字符') / K_FN / K_SHIFT / ...
  → to_utf8() 写入 tty 翻译缓冲 → 应用 read() 拿到 UTF-8
```

- 键表由 `loadkeys`（kbd 包）通过 `KDSKBENT` ioctl 加载，发行版用
  `/etc/default/keyboard` + systemd 配置（`localectl` 设置）；
- `Ctrl+Alt+F3` 切 VT 就是键表里的动作；Magic SysRq 组合（本机 Handlers
  里的 `sysrq`）也在这层。
- **它解释了架构选择**：把策略放进内核 = 每改布局都要动内核；图形时代
  X11/Wayland 把这层整体搬到用户态（X server / compositor + xkbcommon），
  换来生态繁荣。这也是"链路 D"（01 章）的意义。

## 7. 本章源码走读索引

| 关注点 | 文件 | 入口函数 |
|---|---|---|
| 事件 uapi | `include/uapi/linux/input-event-codes.h` | — |
| 事件分发 | `drivers/input/input.c` | `input_event`, `input_pass_values` |
| evdev 字符设备 | `drivers/input/evdev.c` | `evdev_read`, `evdev_ioctl` |
| AT 键盘 | `drivers/input/keyboard/atkbd.c` | `atkbd_interrupt` |
| i8042 控制器 | `drivers/input/serio/i8042.c` | `i8042_interrupt` |
| USB HID | `drivers/hid/usbhid/hid-core.c`, `drivers/hid/hid-input.c` | `hid_input_report` |
| uinput | `drivers/input/misc/uinput.c` | `uinput_ioctl_handler` |
| VT 键盘 | `drivers/tty/vt/keyboard.c` | `kbd_keycode` |

> 获取内核源码：`git clone --branch v6.12 --depth 1
> https://git.kernel.org/pub/scm/linux/kernel/git/torvalds/linux.git`
> （体积较大，本教程未附带；上述函数名在各版本间稳定可搜。）

## 8. 动手实验清单 💻

1. **设备清单与驱动归属**：`cat /proc/bus/input/devices`、`udevadm info -a -n
   /dev/input/event5`——找出 xremap 的父设备（应是 `/dev/uinput`）。
2. **evtest 观察 MSC_SCAN**：分别看 AT 键盘（Set2 `1c`）与 USB 设备（HID
   `700xx`）的原始码差异（见 4.3）。
3. **制造一个虚拟键盘**：跑 2.3 的 Python 例子，`cat /proc/bus/input/devices`
   里出现新设备；再用 `sudo evtest /dev/input/eventN` 看它发的 KEY_A。
   思考：为什么 compositor 毫无感知地接受了它？（本章 2.3 的答案）
4. **观察内核自动重复**：按住一个键，数 `value=2` 的间隔，对照
   `REP_DELAY/REP_PERIOD`。
5. **VT 对照**：`sudo localectl set-keymap de` 后切 VT（Ctrl+Alt+F3）打字
   观察布局变化（图形会话不受影响！内核键表只影响 VT）。

## 9. 本章小结

- 输入子系统 = input_dev/input_handler/input_handle 的发布-订阅三角；
- 设备驱动负责把 scancode/HID usage 翻成 keycode，内核止步于此；
- evdev 是用户态窗口，事件 = `{time, type, code, value}` 流 + SYN_REPORT 分包；
- uinput 证明"设备"是内核里可伪造的抽象（xremap 即实例）；
- VT 是唯一在内核做翻译的路径，作为架构对照组。

下一章进入用户态：[03-userspace-no-ime.md](03-userspace-no-ime.md)
