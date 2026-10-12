# 06 · 配置输入法后的完整链路：复盘与布线总表

> 前五章的零件在这章组装。先讲清"环境变量三件套"在各工具库里到底改变了
> 什么，再把 Wayland 原生 / DBus 模块 / XWayland-XIM / Electron / Flatpak
> 五条实路逐条布线。

## 1. 环境变量：它们到底控制什么

| 变量 | 作用域 | 取值与含义 |
|---|---|---|
| `GTK_IM_MODULE` | GTK3/GTK4 | `ibus`/`fcitx`（装对应 `im-*.so` 模块）/ `xim` / 空。**空或未设时**：Wayland 会话 GTK 用内置 wayland IM context（text-input-v3）；X11 用 simple |
| `QT_IM_MODULE` | Qt5/Qt6 | `ibus`/`fcitx`/`fcitx5`/空。空时 Qt 用 qtwayland 内置 text-input 集成（或 X11 下 compose） |
| `XMODIFIERS` | X11/XIM 系（含 Java、老 Motif） | `@im=ibus` / `@im=fcitx`——XIM 服务器选择，XSetLocaleModifiers 读取 |
| `SDL_IM_MODULE` | SDL2 | `ibus`/`fcitx`——SDL 自带两者的 DBus 客户端 |
| `GLFW_IM_MODULE` | GLFW(打补丁的) | 仅 `ibus`——GLFW 上游长期无 IME 支持的历史包袱 |
| （无变量）Electron/Chromium | — | X11 后端经 LinuxUi 用 GTK IM context（受 `GTK_IM_MODULE` 影响）；ozone-wayland 需 `--enable-wayland-ime`（text-input-v3），可配 `--gtk-version=4` |

**优先级规律**：显式设置的环境变量 > 工具库内置的 Wayland text-input 路径 >
裸键盘。这就是为什么 fcitx5 文档常建议"GNOME/Wayland 下不用设三件套也能
工作（走 text-input 或 waylandim），但设了会更稳（强制走模块，绕开协议差异）"
——代价是失去 compositor 中介带来的候选窗精确定位等好处（04 章已述）。

## 2. 链路 B1：Wayland 原生（text-input-v3，compositor 中介）

以 GNOME + GTK 应用 + IBus 为例（本机默认形态）：

```
[物理键 n]
 evdev → libinput → mutter (XKB state 更新)
 mutter:
   ├─ 全局快捷键判定(Super等)——不消费 n
   ├─ wl_keyboard.key(30) → GTK 应用
   └─ (应用的 text-input 已 enable) 
 GTK: GtkIMContextIMWayland.filter_keypress → 
   text_input_v3.set_surrounding + commit_state(经 zwp_text_input_v3)
 mutter(text-input-v3 服务端) → gnome-shell(IBus 客户端) →
   DBus org.freedesktop.IBus: ProcessKeyEvent(30,…)
 ibus-daemon 路由 → ibus-engine-libpinyin(独立进程)
   词库查询("n") → UpdatePreeditText("n") + UpdateLookupTable
 原路返回: shell → mutter → text_input_v3.preedit_string → GTK 绘制预编辑
[按 Space]
 引擎选词 → CommitText("你") → … → text_input_v3.commit_string("你")
 → GtkEditable 插入 "你"
```

要点：

- 按键实际走了 **两条路**：原始 keycode 给应用（wl_keyboard.key），语义
  请求给 compositor（text-input）。应用把 key 事件再喂给 IM context，
  IM context 通过协议问 compositor"这键你吃吗"——**"吃掉与否"的裁决在
  compositor→IM 一侧**；
- preedit/commit 只走 text-input 一条路，焦点窗口绘制；
- 候选窗：shell（IBus panel 角色）渲染，位置来自 set_cursor_rectangle。

## 3. 链路 B2：DBus 模块直连（环境变量强制，跨 compositor 通吃）

`GTK_IM_MODULE=ibus` 或 `=fcitx` 时，GTK 绕过 text-input：

```
 GTK: GtkIMContextIBus / GtkIMContextFcitx（im-ibus.so / im-fcitx5.so）
   ├─ 启动时连 session DBus（org.freedesktop.IBus / org.fcitx.Fcitx5）
   ├─ 每个编辑框建一个 InputContext 对象（DBus 对象一串）
   └─ filter_keypress → IC.ProcessKeyEvent(keyval, keycode, state) ─┐
                                                                     │ DBus
 输入法框架 ◄──────────────────────────────────────────────────────┘
   (ibus: daemon 转发引擎进程; fcitx5: dbusfrontend addon 进程内直达引擎)
   commit/preedit 回调 → GTK 信号
 候选窗: ibus→panel(GNOME=shell / 其他=ibus-extension-gtk3 自绘)
         fcitx5→classicui 自绘(GTK 窗口)
```

与 B1 的差异：compositor 被架空，光标位置靠应用上报（IBus 的
SetCursorLocation / fcitx5 的 SetCursorRect），XKB 状态靠模块自己从 GDK 事件
推。**任何桌面行为一致**——这是兼容性最大公约数的真实含义。

## 4. 链路 C：XWayland 应用 + XIM（化石路线）

X11 应用在 Wayland 会话里由 Xwayland 承载，输入法对接口是 1990s 的 XIM：

```
 mutter → wl_keyboard → Xwayland(本质是个 Wayland 客户端)
   → 翻译成 X11 KeyPress(keycode+8) → XNextEvent 到应用
 应用启动时: XSetLocaleModifiers("@im=ibus")   ← XMODIFIERS 环境变量进入协议
   → XCreateIC(ximServer="ibus", preedit…)
   → XFilterEvent() 把事件先交给 XIM 服务器(ibus-x11 / fcitx5 xim addon)
 XIM 服务器(与 Xwayland 讲 X 协议) → DBus → 输入法框架 → 引擎
 commit 路径: XIM 用 XSendEvent/合成 KeyPress 把成品文本"打"回应用窗口
```

XIM 的三大遗产问题（排障时先想到它）：

1. **候选窗不跟随光标**：光标位置靠应用主动上报（XIM spot），许多应用不报
   ——候选窗只能呆在角落；
2. **焦点/焦点预编辑丢失**：XIM 无每上下文状态同步，切换窗口 preedit 常丢；
3. **keycode+8 偏移、keyval 语义**：与 Wayland 原生键不同源，快捷键兼容性怪象
   （比如某些组合键在 XWayland 应用里触发输入法切换）多源于此。

排障口诀：**能 Wayland 原生就别 XWayland，能模块/XIM 之外就别 XIM**。

## 5. 链路 E：Electron / Chromium / 特殊应用

- **X11 后端**（默认臭氧层 x11）：经 LinuxUi 走 GTK IM context，
  `GTK_IM_MODULE` 有效；
- **ozone-wayland**：启动加 `--enable-wayland-ime`（新版本默认逐步开启）
  启用 text-input-v3；配合 `--wayland-text-input-version=3` 显式指定；
- VSCode：`code --ozone-platform-hint=auto --enable-wayland-ime`（或写进
  `~/.config/code-flags.conf`，发行版打包通用）；
- Java（IDEA 等）：X11 时代靠 XIM/XIM plugin；Wayland 下跑 XWayland，
  `XMODIFIERS` 必须正确，JVM 参数 `-Dawt.ime...` 视版本而变（重灾区，
  08 章故障树收录）。

## 6. 链路 F：Flatpak/沙箱应用

沙箱没有 session DBus 直连权利，走 portal：

```
flatpak 应用(GTK) → DBus → org.freedesktop.portal.IBus(ibus-portal)
  → 校验/过滤(引擎白名单、surrounding 剥离等安全策略) → 真 ibus-daemon
fcitx5: org.freedesktop.portal.Fcitx(自带同名 portal 接口)
```

后果：沙箱内引擎列表/热键与宿主不完全一致；surrounding text 默认被剥
（隐私策略）——整句转换质量可能下降。这是"输入法在 Flatpak 应用里怪怪的"
的机制解释。

## 7. 按键消费顺序总表（"谁吃掉了我的按键"）

从内核到应用，一颗按键在路上遇到的"收费站"依次是：

```
1. 内核 evdev grab?            (xremap 等 EVIOCGRAB —— 02章)
2. compositor 全局快捷键        (mutter/KWin/sway bindsym)
3. IME 框架全局键               (fcitx5 全局配置的触发键/ibus 热键)
4. waylandim grab / text-input (04章: 输入法接管原始键盘)
5. 应用 IM context 链           (本章 B1/B2: filter_keypress 返回 TRUE?)
6. 应用快捷键                   (菜单加速键, Ctrl+S…)
7. 文本插入                     (keyval→UTF-8 上屏)
```

排障时从下往上问："这键到底停在了哪一站？"（08 章给每层的观测方法。）

## 8. 布线总表（速查）

| 应用形态 | 推荐路径 | 依赖设置 |
|---|---|---|
| GTK3/4 @ GNOME | text-input-v3（shell→IBus） | 什么都不设（默认最优） |
| GTK3/4 @ KDE/wlroots | waylandim（fcitx5）或模块 | 不设变量走 IM 协议；或设 `GTK_IM_MODULE=fcitx` |
| Qt5/6 @ Wayland | qtwayland text-input 或模块 | 同上（`QT_IM_MODULE`） |
| SDL2 游戏 | SDL 的 DBus IM | `SDL_IM_MODULE=fcitx` |
| Electron @ Wayland | ozone text-input-v3 | `--enable-wayland-ime` |
| Electron @ X11 | GTK IM via LinuxUi | `GTK_IM_MODULE` |
| X11 应用 @ Xwayland | XIM | `XMODIFIERS=@im=fcitx` + xim addon |
| Flatpak 应用 | portal | 宿主框架对应 portal 包 |
| 纯终端 tty | （无 IME）内核键表 | 无（02 章链路 D；fbdev 下无输入法） |

## 9. 动手实验 💻

```bash
# 1) 对比"设与不设"GTK 路径差异（同应用两个实例）
GTK_IM_MODULE= gedit &          # text-input-v3 路径
GTK_IM_MODULE=ibus gedit &      # DBus 模块路径
#   打字观察: 候选窗位置精度、切换键行为差异

# 2) 观察 DBus 流量（B2 路径实证）
busctl --user monitor 2>/dev/null | grep --line-buffered -iE "ibus|fcitx" | head -50

# 3) XWayland 应用检查（链路 C 实证）
xprop | grep -i xim             # 点一个 X11 应用窗口, 看 XIM_SERVERS
xdotool key --clearmodifiers n  # Xwayland 下注入按键测试 XIM 链

# 4) 环境体检: 确认图形会话里三件套的真实值
#   (在图形会话终端里, 不是 SSH 会话!)
env | grep -E "IM_MODULE|XMODIFIERS"
```

## 10. 本章小结

- 三件套的本质是"工具库选择 IM 客户端实现"的开关，未设时 Wayland 工具库
  默认走 compositor 中介（text-input-v3）；
- 五条实路（B1/B2/C/E/F）殊途同归：都终结于"框架拿到按键→引擎产字→
  preedit/commit 回流"；
- 每条路径的弱点都可预测：B1 看协议实现完整度、B2 看模块兼容性、C 看应用
  是否守规矩报光标、E/F 看打包配置；
- 按键消费顺序表是排障的心智模型。

分层排障工具见：[08-debugging.md](08-debugging.md）
