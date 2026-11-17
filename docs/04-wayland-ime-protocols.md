# 04 · Wayland 输入法协议森林：text-input 与 input-method

> Wayland 核心协议里**没有输入法**。这块功能由一堆"不稳定"扩展协议拼出来，
> 且至今没有一统天下的版本——这是 Wayland 下输入法兼容性问题的总根源。
> 本章把协议森林讲清楚，然后给出桌面×输入法的支持矩阵。

## 1. 为什么核心协议不带输入法

Wayland 设计哲学：核心最小化，一切策略外置。输入法恰好卡在**敏感的中游**：

- 输入法需要拿到用户的**所有按键**（潜在敏感）；
- 需要在屏幕上**画候选窗**（越过应用窗口，位置要贴着光标）；
- 需要和应用交换**编辑框上下文**（surrounding text，也是隐私）；
- 桌面环境各自有自己的输入法集成历史（GNOME/IBus、KDE/自研、wlroots/中立）。

于是协议被拆成两个方向 + 若干辅助协议，**compositor 是强制中介**：

```
                        ┌──────────────────────┐
   应用（GTK/Qt/终端）  │      Compositor      │   输入法程序
   ┌──────────┐        │                      │      ┌──────────┐
   │ GtkEntry │◄───────┤  mutter / KWin / sway├─────►│ fcitx5   │
   └──────────┘ zwp_   └──────────────────────┘ zwp_ └──────────┘
     text_input_vN         (转发/翻译/拼候选窗)  input_method_vN
        "我是谁？我在编辑什么？"                  "把按键给我，我给你文字"
```

- **text-input**（应用→compositor）：应用声明"我是个编辑框，我要输入法，
  我的光标在 (x,y)，周围文本是…"；
- **input-method**（compositor→输入法）：compositor 把按键和编辑框状态喂给
  输入法，输入法回吐 preedit/commit；
- 候选窗：输入法自己画（layer-shell 或 xdg_popup），compositor 通过
  input_panel 协议告知可用区域。

## 2. text-input 的三代

三代都不在核心协议里，v3 至今仍在 wayland-protocols 的 *unstable* 目录。

| | v1（weston 系） | v2（wlroots 系） | v3（现行标准） |
|---|---|---|---|
| 归属 | weston 私有→进过 unstable | wlr-protocols | wayland-protocols unstable |
| 绑定对象 | wl_seat | 每个 wl_surface 一个 | 每个 wl_seat 一个 + set_surface |
| 状态提交 | 直接生效 | 直接 | **双缓冲**（请求积攒，`commit_state` 才生效） |
| surrounding | ✔ | ✔（带光标位置） | ✔（v3 语义更严） |
| 输入类型提示 | hints | purpose + hints | purpose + hints |
| preedit 样式 | preedit_styling | 无 | 无（样式被砍） |
| 主要用户 | 老 weston/SDL 早期 | GTK3 wayland 后端曾用 | **GTK3/4、Qt、SDL2/3、Chromium(ozone)** |
| 现状 | 淘汰中 | 基本淘汰 | 事实标准 |

### v3 的关键消息（应用视角）

```
应用 → compositor:
  enable / disable                       进入/离开可输入状态
  set_surrounding_text(text, cursor…)    上下文（整句转换用）
  set_content_type(hint, purpose)        hint=none/…. purpose=free_text/password/…/terminal
  set_cursor_rectangle(x,y,w,h)          候选窗贴哪里（相对 surface 坐标）
  commit_state(serial)                   本轮状态生效（双缓冲点）
  reset                                  放弃当前 preedit

compositor → 应用:
  enter(serial, surface) / leave         text-input 焦点切换
  preedit_string(serial, text, commit)   预编辑串（commit 字段很少用）
  commit_string(serial, text)            提交最终文本
  delete_surrounding_text(before, after) 让应用删上下文（替换式转换）
```

v3 的工程怪癖（读源码/调试时的坑）：

- **双缓冲**：所有 set_* 在 `commit_state` 前不生效——忘记 commit 是新手
  应用最常见的 bug（表现：光标矩形不更新）；
- preedit 没有"隐藏"专用信号，空 preedit_string 表示取消；
- `disable` 之后 `enable` 的状态保持语义曾长期各 compositor 理解不一致。

## 3. input-method 的两代

### v1（zwp_input_method_v1，weston 出身）

compositor 主动 `bind` 唯一的输入法；核心是 `zwp_input_method_context_v1`
——输入法通过它拿到一个"伪 wl_keyboard"（keymap、key、modifiers、
`grab_keyboard`），并把结果写回（preedit_string/commit_string）。
**KWin（Plasma 5/6）至今用它**支持第三方输入法（maliit 键盘、fcitx5）。

### v2（zwp_input_method_v2，wlroots 系）

- 输入法主动 `get_input_method(seat)`；
- `zwp_input_method_v2`：`activate/deactivate` + 双缓冲 `commit_state`；
- **`grab_keyboard` → `zwp_input_method_keyboard_grab_v2`**：输入法接管原始
  按键流（等价被 grant 的 wl_keyboard），不需要 text-input 的应用（老终端、
  游戏）也能被输入法服务——grab 模式下输入法自己模拟"直通键入"；
- `zwp_input_panel_v2` + `zwp_input_popup_surface_v2`：候选窗定位。
  sway/Hyprland 等所有 wlroots 系 compositor 实现它，**fcitx5 的
  `waylandim` addon 实现了 v2 客户端**（`third_party/fcitx5/src/frontend/
  waylandim/waylandimserverv2.cpp`——本仓库源码可查）。

### v3？—— 不存在 input-method-v3 进 wayland-protocols 的定论版本；

这是当前生态最大的一块悬而未决：input-method 至今是"非上游"协议。

> **源码印证**（third_party/fcitx5/src/frontend/waylandim/）：
> `waylandimserver.cpp` 绑定 `ZwpInputMethodV1`（服务 Plasma/Weston），
> `waylandimserverv2.cpp` 绑定 v2（服务 wlroots 系）；另有
> `plasmaappmonitor / wlrappmonitor / cosmicappmonitor` 分别适配各家
> compositor 的应用焦点监控接口。一个 addon 同时讲两种方言，就是协议
> 碎片化在代码里的直接体现。

## 4. 辅助协议

| 协议 | 作用 |
|---|---|
| `zwp_input_panel_v1/v2` | 候选窗/软键盘的锚定区域 |
| `zwp_virtual_keyboard_v1` | 造虚拟键盘注入按键（ydotool、fcitx5 的键盘注入后备、远程桌面） |
| `zwp_text_input_v1`（存档） | 极老应用兼容 |
| `xdg_shell`（无关但常一起提） | 候选窗若用弹出窗口实现时的定位来源 |
| `wlr_layer_shell`（wlroots） | fcitx5 候选窗悬浮的第三条路（不依赖 input_panel） |

## 5. 平行通道：绕过 compositor 的 DBus 路线

text-input/input-method 是"走 compositor"的官方路线，但两大框架还有
**应用直连输入法的 DBus 通道**（01 章链路 B 的基础）：

| 框架 | DBus 服务 | 应用侧接入 |
|---|---|---|
| IBus | `org.freedesktop.IBus`（session bus）；flatpak 走 `org.freedesktop.portal.IBus`（ibus-portal 过滤转发） | GTK `im-ibus.so`、Qt ibus plugin、ibus IM module |
| Fcitx5 | `org.fcitx.Fcitx5`；自己的 portal 接口 | GTK `im-fcitx5.so`、Qt fcitx5 plugin |

为什么 DBus 通道仍是主流？

1. 不依赖 compositor 实现 input-method（**兼容性最大公约数**）；
2. 功能比 text-input-v3 全（IBus 协议带 preedit 样式、属性、面板控制）；
3. 历史包袱：两框架诞生时 Wayland 输入法协议还不存在。

代价：候选窗定位、焦点跟踪要自己想办法（fcitx5 在 GNOME 上靠 GTK 模块
拿光标位置；ibus 在 GNOME 上由 shell 全权代理）。

## 6. 桌面 × 输入法 支持矩阵（规律比记忆重要）

| Compositor | text-input（应用侧） | input-method（输入法侧） | IBus 地位 | Fcitx5 地位 |
|---|---|---|---|---|
| **GNOME (mutter)** | **v3** ✔ | **不实现**（gnome-shell 直接内嵌 IBus，DBus 私有集成） | 原生（一等公民） | 走 GTK/Qt DBus 模块；或用 fcitx5 的 `ibusfrontend` 冒充 ibus 接管 shell 集成 |
| **KDE (KWin/Plasma)** | v2(老)/v3 ✔ | **v1** ✔ | 仅 DBus 模块 | 原生（waylandim-v1），KCM 有输入法页 |
| **wlroots 系**（sway/Hyprland…） | v2/v3 ✔ | **v2** ✔ | 仅 DBus 模块（候选窗弱） | 原生（waylandim-v2），事实标配 |
| weston | v1/v2/v3 | v1 | — | 原生（waylandim-v1） |

三条规律：

1. **GNOME 例外论**：唯一不实现 input-method 的主流桌面——它把 IME 位置
   留给了内嵌 IBus。第三方输入法在 GNOME 只能走应用侧通道；
2. **fcitx5 全家桶论**：fcitx5 同时实现 IM-v1 + IM-v2 + DBus + XIM + IBus
   协议前端（`src/frontend/` 下五个目录：dbusfrontend/waylandim/xim/
   ibusfrontend/fcitx4frontend），所以它能在所有桌面达到"原生级"体验；
3. **IBus 依赖 GNOME 论**：ibus 不实现任何 wayland input-method 客户端，
   离开 GNOME 后只剩应用侧 DBus 模块，候选窗与焦点集成明显劣化。

## 7. 三种部署形态的消息序列（把协议串起来）

### 形态一：sway + fcitx5（input-method-v2，纯协议路线）

```
GTK 应用            sway                fcitx5
   │ text_input_v3.enable+set_cursor+commit_state │
   ├──────────────────►│                      │
   │                   │ im_v2.activate       │
   │                   ├─────────────────────►│
   │ key(KEY_N) ◄──────┤◄── wl_keyboard(30)   │ (grab 键盘，原始码直达)
   │                   │ im_v2.commit_state + │
   │                   │   surrounding(text-input 转发)
   │                   │◄─ keystroke 'n'      │
   │                   │  …组合 "ni"…         │
   │ preedit_string("ni")                     │
   │◄──────────────────┤◄─────────────────────│
   │ (用户按 1/Space)  │◄─ commit("你")       │
   │ commit_string("你")                       │
   │◄──────────────────┤                      │
```

### 形态二：GNOME + IBus（shell 内嵌，DBus 路线）

```
GTK 应用        mutter/gnome-shell           ibus-daemon → 引擎进程
   │ GTK_IM_MODULE=ibus（或 wayland text-input-v3）│
   ├────── DBus: IBus.InputContext.Create ─────────►│
   │ focus_in/set_cursor_location                   │ ibus-libpinyin(独立进程)
   │ ProcessKeyEvent(keycode, state) ──────────────►│ 查词库→候选
   │◄─── CommitText("你") / UpdatePreeditText ──────│
   │  (候选窗: gnome-shell 作为 ibus panel 渲染)    │
```

要点：引擎（ibus-engine-libpinyin）是**独立进程**，由 ibus-daemon 按需拉起
（05 章展开）。

### 形态三：GNOME + fcitx5（DBus 应用侧路线）

同形态二的形状，但 DBus 服务是 `org.fcitx.Fcitx5`，候选窗由 fcitx5 自绘
（GTK 窗口 + layer-shell/定位变通）。若启用 fcitx5 `ibusfrontend`，则
gnome-shell 以为自己在和 ibus 说话——fcitx5 借壳获得 shell 级集成。

### 分野：键在哪被截住？——一问穿透三形态

问一个直击本质的问题：**开了输入法，应用还能从 `wl_keyboard` 收到
keycode 吗？** 三种形态答案不同，而全部差异只有一个来源——**谁截住了
键**。libinput 那一半（03 章）永远不变：compositor 始终第一个拿到全部
原始键；变的只是截住点，与其后的搬运协议：

| | 形态一：协议路线（sway + fcitx5） | 形态二/三：DBus 路线（GNOME + ibus/fcitx5） |
|---|---|---|
| **截住位置** | **compositor 内**（键分发策略层，grab 先于转发） | **应用进程内**（GTK/Qt IM 模块，toolkit 事件层） |
| **搬运协议** | Wayland：text_input_v3 + input_method_v2 | DBus：`org.freedesktop.IBus` / `org.fcitx.Fcitx5` |
| **compositor 知情吗** | 全程知情，activate/preedit/commit 都经它手 | 毫不知情，`wl_keyboard.key` 照发给焦点窗口 |
| **被消费的键** | **到不了应用**——compositor 侧吃掉，应用只见 preedit/commit | **照样到达应用进程**，只是被进程内 IM 模块截走，编辑框看不见 |
| **候选窗定位** | 协议级：`set_cursor_rectangle` → input_panel/layer-shell | 自谋出路：GTK 模块拿光标位置，或 gnome-shell 代理 panel |
| **焦点跟踪** | compositor 原生（activate/deactivate） | 应用主动上报 focus_in |
| **兼容性** | 取决于 compositor 实现 input-method | 只要用 GTK/Qt 就通（主流路线的根源，§5 三条理由） |

对"两级翻译"（libinput 管 compositor 侧、xkbcommon 管应用侧）的影响：

- **形态一断在服务端**：被 grab 的键在 compositor 侧就被 IM 消费，应用的
  xkbcommon 见不到它们——对这些键两级翻译退化为服务端一级，text_input
  通道直接送成品字符串；
- **形态二/三断在应用内**：`wl_keyboard` + xkb 翻译照常走完
  （keycode→keysym→字符都算出来了），IM 模块拿翻译结果去查词库——
  IBus 的 `ProcessKeyEvent` 传的已是按键语义。

推论：**IME 开启后 `wl_keyboard` 并不关闭，它只是从"文字的来源"降级为
"纯按键语义的来源"**。编辑框必须双通道并存：

| 通道 | 管什么 | IME 开启后 |
|---|---|---|
| `wl_keyboard` + xkb | "键"：快捷键、光标移动、终端/游戏全量按键 | 协议路线下只剩未被消费的键；DBus 路线下全量仍在 |
| `zwp_text_input_v3` | "字"：组合好的字符串 | 拼音/日文正文的唯一来源 |

> **模糊地带**：GNOME 上 GTK 应用也可走 text_input_v3（mutter 实现了它），
> mutter 把文本输入转发给 shell 内嵌的 ibus——截住点又回到 compositor 侧。
> 可见分类的关键维度不是"哪个桌面"，而是"谁截住了键"。

## 8. 动手实验 💻

```bash
# 1) 查看你的 compositor 检查了哪些输入法协议（图形会话内运行）
$ sudo apt install wayland-utils
$ wayland-info | grep -E "text_input|input_method|virtual_keyboard"
# GNOME 预期: zwp_text_input_v3 ✔；无 input_method*
# sway 预期:  zwp_text_input_v2/v3 + zwp_input_method_v2 + virtual_keyboard

# 2) wev 观察 text-input 事件（wev 绑定 v3，聚焦输入框打字看 preedit/commit）
$ sudo apt install wev && wev

# 3) GNOME 下看 shell 与 ibus 的 DBus 对话
$ busctl --user monitor 2>/dev/null | grep -E "IBus|Fcitx" &
# 然后在任意输入框打几个拼音，观察 ProcessKeyEvent/CommitText 流
```

## 9. 本章小结

- 输入法协议 = text-input（应用侧，v3 是事实标准）+ input-method（输入法侧，
  v1=Plasma/weston、v2=wlroots，**没有统一上游版本**）；
- GNOME 不实现 input-method，靠 shell 内嵌 IBus；这是 IBus 强绑定 GNOME、
  fcitx5 必须自带五种前端的历史原因；
- 双框架都有绕过 compositor 的 DBus 应用侧通道，这是当前实际主流路线；
- 一次拼音输入的消息序列在三种部署形态下形状不同、语义同源；
- 三形态的分野在**截住点**：协议路线在 compositor 截键（被消费的键到不了
  应用），DBus 路线在应用进程内截（wl_keyboard 全量照达）；IME 开启后
  wl_keyboard 并不关闭——"键"与"字"双通道并存（§7 分野表）。

下一章正式对比两大框架：[05-ibus-vs-fcitx5.md](05-ibus-vs-fcitx5.md)
