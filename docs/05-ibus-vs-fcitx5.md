# 05 · IBus 与 Fcitx5：架构对比与选型

> 两大框架解决的 是同一个问题（按键→文字的中介调度），但架构哲学几乎相反：
> **IBus = 微内核总线**（守护进程+引擎进程+DBUS 协议），**Fcitx5 = 单体插件**
> （一个进程内动态加载 addon）。本章拆解两者，最后给选型建议。

## 1. 历史与定位

| | IBus | Fcitx5 |
|---|---|---|
| 诞生 | 2007，Red Hat 主导（为 CJK 输入标准化，取代老旧 iiBus? 实为接替 iiimf/SCIM） | fcitx 2002 起源于个人项目（Yuking）；2016–2020 由 Weng Xuetian (csslayer) 完全重写为 fcitx5 |
| 治理 | GNOME 生态一环（GNOME 默认，Fedora/RHEL 默认），freedesktop.org 托管 | 社区主导（csslayer 为核心维护者），fcitx-for-freedesktop GitHub org |
| 哲学 | 框架极简，策略放外部进程 | 功能内聚，一切皆 addon |
| 教程环境 | 已装且运行中（GNOME 默认） | 已装未启用 |

## 2. IBus 架构拆解

### 2.1 进程拓扑（本机 `ps` 实测）

```
ibus-daemon --panel disable -r --xim     ← 总线/调度器（--panel disable 因为 GNOME shell 兼任面板）
├── /usr/libexec/ibus-dconf              ← 配置后端（dconf 代理）
├── /usr/libexec/ibus-extension-gtk3     ← 扩展（emoji/unicode 面板等）
├── /usr/libexec/ibus-portal             ← flatpak/沙箱应用的安全代理
├── /usr/libexec/ibus-x11 --kill-daemon  ← XIM 协议桥（Xwayland 应用走这）
└── /usr/libexec/ibus-engine-simple      ← 引擎示例："键盘直通"引擎(xkb:us::eng)
    （切到拼音时会再拉起 ibus-engine-libpinyin 进程）
```

**每个引擎是独立进程**——拼音引擎崩溃不会拖死总线；引擎可以用任何
GObject Introspection 语言写（C/Python/JS…）。

### 2.2 客户端库与 DBus 协议

`third_party/ibus/src/` 是 libibus（GObject 库 + GI 绑定）：

- `ibusbus.c` — 连接 ibus-daemon（`org.freedesktop.IBus`）；
- `ibusinputcontext.c` — **InputContext** 抽象：应用侧的编辑框代理。
  核心方法：`ProcessKeyEvent(keyval,keycode,state)`、`FocusIn/FocusOut`、
  `SetCursorLocation`、`SetSurroundingText`、`Reset`；
  回调：`CommitText`、`UpdatePreeditText`、`UpdateAuxiliaryText`（联想串）、
  `UpdateLookupTable`（候选表）；
- `ibusengine.c` — 引擎侧基类：框架把按键喂给 `process_key_event`，引擎
  通过 `update_preedit/commit_text/update_lookup_table` 应答；
- `ibusemoji.c / ibuscomposetable.c` — 框架自带的 emoji/死键组合支持。

协议是**双向 DBus**：应用与引擎互相调用，ibus-daemon 做路由。候选窗由
Panel 服务渲染——GNOME 下就是 gnome-shell 自己（shell 内嵌 IBus client，
这也是 04 章"GNOME 不实现 input-method"的下半句：不需要，shell 全包了）。

### 2.3 配置与集成

- 配置存 dconf（gsettings `org.freedesktop.ibus.*`）；引擎各自有 schema；
- 输入源列表在 gsettings `org.gnome.desktop.input-sources`，shell 据此
  切换引擎（Super+Space）；**框架本身没有 GUI 候选窗皮肤概念**（交给桌面）；
- Wayland：mutter 实现 text-input-v3，shell 把它桥到 IBus（应用无感知）。

## 3. Fcitx5 架构拆解

### 3.1 单进程 + 五类 addon

`third_party/fcitx5/src/` 布局即架构：

```
src/lib/fcitx/          核心抽象（AddonManager, InputContext, CandidateList,
                        FocusGroup, Event, Action, Instance…）
src/frontend/           ① 前端 addon —— "各种协议的接入点"
    ├── dbusfrontend/       org.fcitx.Fcitx5 DBus（GTK/Qt 模块讲这个）
    ├── waylandim/          zwp_input_method v1+v2（04 章主角）
    ├── xim/                XIM（Xwayland/纯 X11）
    ├── ibusfrontend/       冒充 ibus-daemon（接管 gnome-shell 集成）
    └── fcitx4frontend/     fcitx4 协议兼容层
src/im/                 ② 引擎 addon
    └── keyboard/           键盘直通引擎（含 XKB 布局管理）
    （拼音/五笔/…在 fcitx5-chinese-addons 仓库）
src/modules/            ③ 功能模块：clipboard/quickphrase/unicode/spell/
                        notifications/notificationitem/imselector/emoji/dbus/
                        wayland/xcb …
src/ui/                 ④ UI 插件（classicui 自绘候选窗 / kimpanel 面板协议）
src/tools/              fcitx5-remote、诊断等
⑤ loader addon —— dlopen 机制本身
```

- **一切皆插件**：前端、引擎、模块、UI 都是 `.conf 描述文件 + .so`，由
  `AddonManager` 按依赖加载（`third_party/fcitx5/src/lib/fcitx/addonloader.cpp`）；
- **InputContext** 统一抽象（`inputcontext.cpp`）：无论来自 DBus、waylandim
  还是 XIM，进入核心后都是同一种上下文对象；每个程序独立记忆状态
  （"在终端用英文、在浏览器用中文"）；
- 事件系统（`event.cpp`）实现拦截器链：全局快捷键、快速短语、剪贴板
  都以事件过滤器形式挂在按键路径上。

### 3.2 配置与数据布局

```
~/.config/fcitx5/     profile（输入法列表与顺序）、config、各 addon 配置
                      （人类可读文本，config.desc 描述文件驱动 GUI 生成）
~/.local/share/fcitx5/  用户数据：词库、快速短语、剪贴板历史…
/usr/share/fcitx5/      系统级 addon/数据/图标
```

配置是**纯文本文件**（`fcitx5-configtool` 只是编辑器）——可 diff、可同步、
可版本控制，这是相对 dconf 的运维优势。`fcitx5-diagnose` 输出全链路体检
报告，是排障第一入口。

### 3.3 生态工具链

- `fcitx5-remote`（shell/vim 集成的基础，根目录笔记的 fcitx5 版就是它）；
- quickphrase（任意引擎下 `;` 快速短语）、clipboard、unicode、emoji 输入；
- lua 模块（fcitx5-chinese-addons 提供，可写小脚本扩展）；
- cloudpinyin（云候选）、chttrans（简繁）、pinyinhelper（反查/注音）。

## 4. 正面对比

| 维度 | IBus | Fcitx5 | 优势方 |
|---|---|---|---|
| 进程模型 | daemon+每引擎一进程 | 单进程多 addon | fcitx5（切换引擎零 fork，IPC 开销少）/ ibus（故障隔离好） |
| 协议覆盖 | DBus(+portal)+XIM | DBus(+portal)+XIM+**waylandim v1/v2**+**ibus 模拟**+fcitx4 | **fcitx5**（Wayland 原生路线完备） |
| GNOME 集成 | 原生（shell 内嵌、设置面板、候选窗=shell） | 借壳（ibusfrontend）或应用侧 | **IBus** |
| KDE/wlroots 集成 | 仅应用侧模块 | KCM 配置页 + input-method 原生 | **fcitx5** |
| 引擎开发 | GObject C / GI（Python 等）；文档少 | 现代 C++（addon API + fcitx-utils）；文档少 | 平手（ibus 上手 Python 更快） |
| 配置体系 | dconf（二进制数据库，命令行不友好） | 纯文本+描述文件+GUI | **fcitx5**（可 git 化） |
| 词库工具链 | 各引擎自带（libpinyin 只有 GUI 导入导出） | libime 统一格式+CLI 工具+活跃数据包（zhwiki 词库月更） | **fcitx5**（07 章专题） |
| 可扩展性 | 引擎外部进程即可扩展 | addon+lua 脚本 | fcitx5 |
| 沙箱 | ibus-portal（flatpak 官方 portal） | 自带 portal 接口 | 平手 |
| 多语言 | Mozc/Anthy/Hangul…引擎齐全 | 同样齐全（各自 addon） | 平手 |
| 性能 | 每键跨进程 DBus 往返 | 前端在进程内（DBus 时一次往返；waylandim 零 DBus） | **fcitx5** |
| 代码规模（实测 cloc 口径） | ibus 核心 ~76k 行 C | fcitx5 ~99k + libime ~22k + chinese-addons ~16k C++ | — |
| 教程环境状态 | 运行中 | 已安装未启用 | — |

## 5. 本机切换实测（Debian im-config 机制）

Debian 用 `im-config` 统一管理框架选择（本质是生成环境变量）：

```bash
$ im-config -m                    # 查看当前与可用模式
$ im-config -n fcitx5             # 写入 ~/.xinputrc（下次登录生效）
$ im-config -n ibus               # 切回
# 也可以直接改会话环境变量对比（不改系统）:
GTK_IM_MODULE=fcitx QT_IM_MODULE=fcitx XMODIFIERS=@im=fcitx 你的应用 &
```

GNOME 注意事项：shell 内嵌的 IBus 与 gsettings input-sources 强耦合，
在 GNOME 上长期启用 fcitx5 的稳妥姿势是：

1. `im-config -n fcitx5` + 注销重登；
2. gsettings 输入源只留一个键盘布局（避免 shell 抢 Super+Space）；
3. 可选启用 fcitx5 `ibusfrontend` 让 shell 键位/指示继续工作。

（切换是可逆实验，08 章调试清单还会计账。）

## 6. 选型建议

**选 IBus，如果：**
- 桌面是 GNOME 且不想折腾——原生集成（候选窗、设置、锁屏、gnome-shell
  细节）无出其右；
- 主要用系统自带引擎，对词库深度定制无需求。

**选 Fcitx5，如果：**
- 桌面是 KDE / wlroots 系（sway/Hyprland 等）——原生 input-method 支持；
- 重度中文输入、在意词库可维护性/可迁移性（07 章的核心论点）；
- 想参与输入法开发：现代 C++、纯文本配置、活跃的工具链与数据生态，
  贡献路径更友好（09 章）。

**本教程的立场**：结合你的目标（词库维护 + 可能参与开源维护），**fcitx5
生态（fcitx5 + libime + chinese-addons）是更合适的选择**——理由在第 07、09
章用源码与数据展开。

**一个重要的例外**：目标桌面是 **GNOME（Wayland）** 时，结论反转——GNOME
Shell 只内建了 text-input-v3 → ibus 这条通路，没有 input-method-v2 服务端，
fcitx5 在 GNOME Wayland 下要走 XWayland 或对抗 shell 集成；而给 ibus 写
引擎进程反而"零对抗"：注册组件即得原生候选窗与输入源管理。这个判断在
[10-ibus-qingjian.md](10-ibus-qingjian.md) 里用一次完整实践验证过
（含 GNOME 下 ibus-daemon 由 systemd 用户单元管理等部署细节）。

## 7. 动手实验 💻

1. `ps aux | grep -E "ibus|fcitx"`：对照 2.1 拓扑确认本机进程；切一次
   `ibus engine libpinyin`，观察新引擎进程被拉起（ibus 的进程模型实证）。
2. `busctl --user tree org.freedesktop.IBus | head -20`：看 IBus 的 DBus
   对象树（InputContext 对象按应用一串）。
3. `fcitx5-diagnose | head -80`（若先按第 5 节启用）：感受两者排障工具的差距。
4. 临时用 `GTK_IM_MODULE=fcitx gedit` 对比两种框架在同一应用里的行为
   （候选窗样式、切换键、每程序状态记忆）。

## 8. 本章小结

- IBus：总线型微内核，引擎进程化，DBus 协议公开，GNOME 原生集成；
- Fcitx5：单体插件化，五类 addon，协议覆盖最全，配置/词库工具链可文本化；
- 生态位：GNOME←→IBus 相互成就；GNOME 之外 fcitx5 体验全面占优；
- 选型本质是"桌面集成深度 vs 生态开放度"的取舍。

链路复盘见：[06-ime-chains.md](06-ime-chains.md)
