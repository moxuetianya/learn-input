# 10. 实践：给 qingjian 写一个 IBus 前端（ibus-qingjian）

> 本文是本仓库的一次完整实战记录：从零做一个 **IBus 拼音输入法**，覆盖三个目标——
> ① 丰富词库的日常拼音输入；② LLM 云联想（参考 rime-llm-translator）；③ 候选旁英文译词助学（qingjian）。
> 全部已在 `peter@192.168.2.102`（Ubuntu 24.04 / GNOME 46 Wayland / IBus 1.5.29）部署验证。

## 10.1 为什么不从头写引擎，而是「薄 IBus 前端 + qingjian server」

词库、整句转换、用户学习、译词、云联想这些**引擎能力**与输入法框架无关。
qingjian 已经把这一切做成平台无关的 Core，Linux 上以 `qingjian-linux-server`
（Rust，Unix socket 服务）形态存在，fcitx5 的插件只是一层薄壳。
所以做一个 IBus 输入法 = **写一个与 fcitx5 插件对等的 IBus 前端**：

```
GNOME Shell（内建 ibus 集成，候选窗由 shell 画）
   │ DBus (IBus 协议)
ibus-daemon ──exec──> ibus-engine-qingjian（C，47 KB，本仓库 ibus-qingjian/）
                          │  Unix socket + JSON（qingjian Linux 协议 v3）
                          ▼
                    qingjian-linux-server（Rust）
                    ├─ dict.qj 主词库 8.7 万条 + 领域词库 13 万条（mmap）
                    ├─ bigram 语言模型 + 本地神经模型「含章·通变」（后台线程重排）
                    ├─ qingjian-translate：候选译词 annotation
                    └─ qingjian-predict：云联想 CloudPredictor（本仓库补的接线）
```

**性能**：前端只做转发，每键一次 DBus 调用 + 一次 socket 往返（200 ms 截止），
组句期间 80 ms `Poll` 拉取重排/云联想结果；词库 mmap、模型后台加载，
server 常驻内存与引擎能力相关而与 IBus 无关。前端进程 RSS ~3 MB。

## 10.2 三大需求是怎么落地的

| 需求 | 实现 |
|---|---|
| 丰富词库 | 官方数据包 `qingjian-data.tar.gz`（data-v5，sha256 锁定）：主词库、释义表、领域词库、bigram LM、神经整句模型，一次下载全部就位 |
| LLM 预测 | Linux server 此前没接云联想（macOS/Windows 壳有）。本仓库给 `apps/linux/server` 补了 5 处改动（见 10.3），`[predict]` 填 key 即启用；OpenAI 兼容接口 + DeepSeek Flash 默认值 + 300 ms 防抖 + 云词只补第一页末尾槽位不重排本地候选——与 rime-llm-translator 同思路（其推荐就是 DeepSeek），但走的是「停顿自动联想 + 整句 Tab 补全」而非「双击 v 手动呼叫」 |
| 英文译词 | server 帧里现成的 `candidates.items[].translation.senses`，IBus 前端把它拼进候选文本：`你好  hello`；上屏后 `DisplayAcknowledged` 把页上可见译词回报给 server 记词汇本 |

## 10.3 qingjian 侧补丁（submodule 内，本地分支）

云联想对齐 Windows 版 `dispatch/composed` 已有实现：

1. `apps/linux/server/Cargo.toml`：加 `qingjian-predict` 依赖
2. `apps/linux/server/src/main.rs`：按 `[predict] enabled` 装配
   `CloudPredictor` / `CloudGlossFiller`（失败只记日志，输入优先）
3. `dispatch/config.rs`：`cloud_slots` 取自 `[predict] slots`（原来硬编码 0）
4. `dispatch/mod.rs`：`Router::new` 去掉强制清零；`tick()` 里 `poll_glosses()` + `poll_prediction()`
5. `dispatch/composed/mod.rs`：`recompose()` 发 `request_prediction`；
   新增 `poll_prediction`/`cancel_prediction`；`reset_composition` 补 `cancel_prediction`

## 10.4 IBus 前端（`ibus-qingjian/`，C）

- `src/ipc.c`：Unix socket + 4 字节小端长度前缀 + JSON（json-glib），200 ms 截止，
  SO_PEERCRED 校验同 uid——与 fcitx5 插件同一套线协议
- `src/keymap.c`：IBus keyval → 协议 `KeyEvent`（VK 映射与 fcitx5 mapping.cpp 逐条一致）
- `src/engine.c`：IBusEngine 子类。握手序列 `OpenSession → LinuxHello →
  Capabilities → Focus`；按键转发 `Key`；`KeyResult` 里 `outcome/commit/frame`
  的处理；候选点击 `Candidate`、翻页 `Page`；组句 80 ms `Poll`；
  `DisplayAcknowledged` 回报译词；断线清 UI 放行输入
- 渲染：preedit 整段下划线；lookup table 9 格、label 1–9、
  候选文本 = 词 + 译词（生词加 `·生`）；`notice`/整句补全画 auxiliary

### 踩坑记录（IBus 1.5.29 实测）

1. **组件文件必须 `.xml` 后缀**：`ibus_registry_load_in_dir` 只认
   `MAX(filename, filename+len-4) == ".xml"`，`.component` 被静默跳过，list-engine 不显示也无日志
2. **组件的 DBus 名在 `<name>` 子元素**，不是 `<component name=…>` 属性：
   daemon 靠「引擎进程 request_name(组件名)」这一 NameOwnerChanged 把进程与组件
   关联成 factory；名字对不上时 `SetGlobalEngine` 5 秒超时（`Timeout was reached`）
3. **GNOME 的 ibus-daemon 由 systemd 用户单元管理**
   （`org.freedesktop.IBus.session.GNOME.service`，`Type=dbus`）：
   ssh 下控制它用 `systemctl --user restart …` 而非 `ibus restart`
   （后者找不到 daemon 地址文件）；重装组件后要 `rm ~/.cache/ibus/bus/registry`
   （daemon 默认 `--cache auto`，优先读 registry 缓存）
4. daemon 以 `Type=dbus` 跑在 user bus 上时也会维护私有 socket
   （`~/.cache/ibus/bus/dbus-*`，地址文件 `~/.config/ibus/bus/<machine-id>-…`），
   引擎进程靠地址文件发现它
5. `G_DECLARE_FINAL_TYPE` 链父类 autoptr：IBusEngine 是 DERIVABLE 类型，
   GLib 2.80 下会报 `glib_autoptr_clear_IBusEngine` 未声明，用传统类型样板最省事

## 10.5 部署与验证（102 实录）

```bash
# 一键：构建（101）→ rsync → 安装 → 注册 → 重启
./scripts/deploy-102.sh

# ssh 远程检查
ibus engine            # → qingjian（已激活）
systemctl --user is-active qingjian          # server 在跑
ss -x | grep qingjian.sock                    # 引擎↔server ESTABLISHED
~/.local/state/qingjian/logs/server.*.log     # 词库/模型加载日志
```

云联想：`~/.config/qingjian/config.toml` 的 `[predict]` 填 `api_key`（或
`QINGJIAN_API_KEY` 环境变量）、`enabled = true`，`systemctl --user restart qingjian`。

## 10.6 回到那个问题：为什么扩展多用 fcitx，GNOME 却用 ibus？

一句话：**GNOME 是「框架决定生态」，fcitx 是「生态决定框架」。**

- **ibus 的出身**：2008 年 Fedora/Red Hat 为取代 SCIM 而做，进 freedesktop.org，
  GNOME 3（2011）把它定为默认输入法框架并**深度内建**——GNOME Shell 自己实现
  ibus 的候选面板（`--panel disable` 起的就是无面板 daemon）、输入源管理
  （`org.gnome.desktop.input-sources`）、键盘布局与输入法统一切换。在 GNOME
  Wayland 会话里，shell 只内建了 text-input-v3 → ibus 这一条通路，
  ibus 的位置是**架构性的**，不是可替换的默认值。
- **fcitx5 的亲和力**：社区项目（csslayer 等），C++ 插件架构，addon/扩展点密集
  （5 种前端、table/rime/chinese-addons 生态），开发者文档友好、接口稳定，
  自定义候选窗/皮肤/丰富交互容易。所以「长出新的输入法玩法」的项目大多选 fcitx5，
  KDE Plasma 也推荐它。
- **ibus 上做引擎的特点**（本次实践印证）：引擎是 daemon 按需拉起的独立进程，
  协议是 DBus，交互能力受 shell 候选窗限制（比如候选注释只能拼进文本）；
  但换来的是在 GNOME 下「零对抗」——注册组件即得原生候选窗、光标跟随、
  输入源切换。高度定制 UI 的项目会因此流向 fcitx5（fcitx5 在 GNOME Wayland
  下反而要走 XWayland 或对抗 shell，是镜像难题）。
- 详细协议/架构对比见 [05-ibus-vs-fcitx5.md](05-ibus-vs-fcitx5.md)。

## 10.7 性能特征

| 项 | 数值/来源 |
|---|---|
| 前端二进制 | 47 KB；RSS ~3 MB（glib + json-glib） |
| 每键开销 | 一次 DBus + 一次 Unix socket 往返（<1 ms 量级，200 ms 截止兜底） |
| server 启动 | 词库 mmap + LM 5–10 ms + 神经模型后台 60–80 ms（不挡输入） |
| 组句轮询 | 80 ms/拍，仅组句期间；帧 revision 不变不重画 |
| 云联想 | 300 ms 停顿防抖（qingjian-predict 后台线程），不阻塞候选 |
