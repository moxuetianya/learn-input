# 08 · 调试工具箱：分层观测与故障树

> 心法：01–06 章的"按键消费顺序"就是排障的分割线。遇到问题先问
> **"这颗键/这个词停在了哪一层"**，每层只有两三个工具，一层十分钟。

```bash
# 全章工具一次性装齐（教程环境实测缺前三个）
sudo apt install evtest libinput-tools wayland-utils wev \
                 x11-utils xdotool dbus-utils?   # 包名以 apt search 为准
```

## 1. 第 0 层：先收集事实（30 秒体检）

```bash
echo $XDG_SESSION_TYPE                    # wayland 还是 x11？
env | grep -E "IM_MODULE|XMODIFIERS"      # 三件套是否设置、值对不对
pgrep -a ibus-daemon; pgrep -a fcitx5     # 框架在跑吗？哪个？
ibus engine 2>/dev/null || fcitx5-remote -n   # 当前引擎
cat /proc/bus/input/devices | grep -E "^N|^H" # 设备与 handler
```

90% 的"输入法坏了"在这 5 条里现形：会话类型设错、变量指到没装的框架、
两框架同抢、引擎没切上、设备被 grab。

## 2. 内核/设备层：事件存在吗？长什么样？

```bash
# 2.1 evdev 原始事件（scancode/keycode/SYN 包结构都能看）
sudo evtest /dev/input/event0
#   关注: MSC_SCAN(原始码) / EV_KEY value(1按下/0释放/2重复)
#   若无输出 → 事件没到内核(设备/驱动问题) 或被 EVIOCGRAB 独占(查 xremap 等)

# 2.2 libinput 视角（设备抽象/seat 归属/配置项）
sudo libinput list-devices               # 能力矩阵：哪些设备、支持什么
sudo libinput debug-events               # libinput 加工后的事件流
sudo libinput debug-gui                  # 指针/手势可视化

# 2.3 udev 归属
udevadm info -a -n /dev/input/event5 | grep -E "ID_INPUT|SEAT|DRIVERS"
```

判据：evtest 有而 libinput 无 → 设备被 udev 规则忽略（ID_INPUT_* 标签）；
libinput 有而应用无 → 往上走 compositor 层。

## 3. Compositor / Wayland 层：事件发出去没有？

```bash
# 3.1 协议能力盘点（text-input/input-method 是否存在）
wayland-info | grep -E "text_input|input_method|virtual_keyboard"

# 3.2 当一回客户端：wev 看自己收到的全部输入事件
wev          # 聚焦 wev 窗口打字：wl_keyboard.key/modifiers/enter/leave
             # 以及 zwp_text_input_v3 的 preedit/commit（若绑定）

# 3.3 任意 Wayland 应用的协议级 trace
WAYLAND_DEBUG=1 你的应用 2>protocol.log   # 每条 wl_ 消息全记录

# 3.4 compositor 日志（GNOME）
journalctl --user -b /usr/bin/gnome-shell | grep -iE "input|ibus|keymap"
journalctl -b | grep -iE "mutter|i8042|atkbd|usbhid"   # 内核侧输入异常
```

判据：wev/WAYLAND_DEBUG 里能看到 key 事件 → compositor 正常发放；
应用层再丢就是工具库/IM 模块的事。

## 4. X11/XWayland 层（链路 C 专用）

```bash
xev -event keyboard            # 老牌事件查看器（X11 窗口内按键全打印）
xdotool key --clearmodifiers n # 注入按键（测 XIM 链是否活）
xprop | grep -i xim            # 窗口的 XIM 服务器绑定
xlsclients                     # 哪些应用跑在 Xwayland 上
```

## 5. 输入法框架层

### 5.1 IBus

```bash
ibus list-engine                # 引擎注册表（组件缓存: ibus write-cache）
ibus engine pinyin              # 强制切引擎（脚本/排障利器）
ibus restart                    # 重启守护进程
ibus version

# 调试日志（前台跑 daemon，GIO 日志全开）
G_MESSAGES_DEBUG=all /usr/bin/ibus-daemon --replace --verbose -rxd 2>ibus.log
# dconf 里全部配置
gsettings list-recursively org.freedesktop.ibus 2>/dev/null | head -30
# 引擎进程是否被拉起
ps aux | grep ibus-engine
```

### 5.2 Fcitx5

```bash
fcitx5-diagnose                 # ★ 全链路体检报告（环境/变量/协议/词库全查）
fcitx5-remote -n                # 当前输入法名
fcitx5-remote -t                # 切换中英（vim 集成同款 API）
fcitx5 --verbose '*=5' 2>fcitx5.log    # 前台全量日志
journalctl --user -b | grep -i fcitx
ls ~/.config/fcitx5 ~/.local/share/fcitx5    # 配置/数据布局（07 章）
```

## 6. 应用层

```bash
# 6.1 GTK：换 IM 模块对照（最常用的二分法）
GTK_IM_MODULE= 你的应用      # 强制 text-input/simple
GTK_IM_MODULE=ibus 你的应用  # 强制 DBus 模块
GTK_IM_MODULE=xim 你的应用   # 强制 XIM（Wayland 会话下会退化）

# 6.2 Qt：看插件加载
QT_DEBUG_PLUGINS=1 你的qt应用 2>&1 | grep -iE "input|fcitx|ibus"

# 6.3 Electron/Chromium
code --enable-wayland-ime --ozone-platform-hint=auto   # wayland IME 开关

# 6.4 DBus 总线监听（看应用↔框架对话）
busctl --user monitor | grep --line-buffered -iE "ibus|fcitx"
dbus-monitor "session" 2>/dev/null | grep -A3 -iE "InputContext|CommitText"
```

## 7. 常见故障树（症状 → 层 → 验证 → 解法）

| 症状 | 先怀疑层 | 验证 | 常见解法 |
|---|---|---|---|
| 所有应用都无候选，按键直接上屏 | 框架/环境 | 第 0 节体检 | `im-config -n` 选对框架+重登；确认引擎已装（`ibus list-engine`） |
| 只有某应用直接上英文 | 应用 IM 路径 | 第 6 节换模块对照 | 该工具库对应变量/flag（Electron `--enable-wayland-ime` 等，见 06 章布线表） |
| XWayland 应用候选窗不跟随光标 | XIM（链路 C） | `xprop \| grep -i xim` | 属 XIM 固有缺陷：尽量用原生 Wayland 版应用 |
| 切换键(Super+Space/Ctrl+Space)无效 | compositor 抢键 or 框架键位 | 06 章消费顺序 2/3 站 | gsettings 输入源切换键 vs 框架全局键，去重 |
| 升级后用户词全丢 | 词库版本耦合 | 07 章 2.3 | libpinyin: 升级前导出文本；fcitx5: 天然无此问题 |
| VM/远程会话按键丢失或重复 | 设备层 | `evtest` 看原始流 | QEMU 换 usb-tablet/键盘型号；检查 xremap grab |
| Flatpak 应用无输入法 | portal | `flatpak list --app`; 装对应 portal 扩展 | 安装 `ibus` portal 支持 / fcitx5 的 org.freedesktop.portal.Fcitx |
| 终端模拟器无输入法 | 应用自身 | foot/kitty 支持 text-input；alacritty 不支持 | 换支持 IME 的终端（06 章布线表） |
| Vim 中不能按 j/k（被 IM 吃掉） | 应用内 IM 状态 | — | 根目录笔记的 InsertLeave 自动切英文方案 |
| fcitx5 与 ibus 互相打架 | 双框架并存 | `pgrep -a ibus-daemon; pgrep -a fcitx5` | im-config 明确选一个；GNOME 留 ibus，其他桌面留 fcitx5 |

## 8. 一个完整排障示例（本机剧本）

> 症状：GNOME 下 VSCode 里中文候选不出现，其他应用正常。

```bash
# 1. 体检: 会话 wayland ✔ 变量未设 ✔ ibus 在跑 ✔ → 怀疑 Electron 的 IM 路径
# 2. 对照: gedit 正常（text-input-v3 路径），VSCode 是 XWayland+GTK_IM 路径
xprop -id $(xdotool search vscode | head -1) | grep -i xim   # 确认走 XIM?
# 3. 判定: VSCode 缺 wayland IME flag → 06 章链路 E
code --ozone-platform-hint=auto --enable-wayland-ime
# 4. 验证: wayland-info? 直接打字验证候选出现 ✔
# 5. 固化: echo "--enable-wayland-ime" >> ~/.config/code-flags.conf
```

五分钟、四条命令，一个"玄学问题"变成本章流程的机械应用。

## 9. 本章小结

- 排障 = 沿按键消费顺序逐层二分，每层 2–3 个工具足够；
- `fcitx5-diagnose` / `ibus engine` / `wayland-info` / `evtest` / `wev` 是
  五件最高频武器；
- 故障树的右列几乎都能追溯到 02–07 章的某个机制——机制清楚了，故障就
  只是流程。

下一章（终章）：[09-contribution.md](09-contribution.md)
