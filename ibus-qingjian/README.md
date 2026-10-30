# ibus-qingjian — qingjian 的 IBus 前端

一个 47 KB 的 C 程序：把 IBus 的按键/焦点/候选事件转发给 `qingjian-linux-server`
（Unix socket + JSON，与官方 fcitx5 插件同一套线协议），并把 server 回的帧
（preedit / 候选 / 译词 / 提示）画到 IBus 的候选窗。拼音、整句、词库、学习、
译词、云联想全部在 server 里，本前端不含任何输入法逻辑。

```
ibus-daemon ──exec──> ibus-engine-qingjian ──socket──> qingjian-linux-server
```

## 依赖

- 运行：libibus-1.0-5、libjson-glib-1.0-0、libglib2.0-0（GNOME 桌面自带）
- 构建：libibus-1.0-dev、libjson-glib-dev、pkg-config

## 构建与安装

```sh
make                        # 构建引擎与组件描述（版本取 git describe）
make test                   # 协议冒烟：自动起停 qingjian server（DICT=real 用完整词库）
sudo make install PREFIX=/usr/local   # 二进制进 /usr/local/bin，组件进 /usr/share/ibus/component/qingjian.xml
ibus-engine-qingjian --version        # 远程排查时确认部署的版本
```

可覆盖变量：`SERVER_BIN`（server 路径）、`QJ_DIR`（qingjian 仓库根）、
`VERSION`（组件版本）。上级目录的 `Makefile` 已把这些串好
（`make smoke` / `make deploy` / `make deploy-engine`）。

注意三件事（全部实测踩过，详见 learn-input/docs/10-ibus-qingjian.md）：

1. 组件文件必须以 **`.xml`** 结尾，否则 ibus-daemon 静默忽略；
2. 组件的 DBus 名（`<name>` 子元素）必须与 `src/main.c` 里 `ibus_bus_request_name`
   的名字一致（`org.qingjian.IBus.Qingjian`），否则 `SetGlobalEngine` 5 秒超时；
3. GNOME 下 daemon 由 `org.freedesktop.IBus.session.GNOME.service` 管理，
   装完组件 `rm -rf ~/.cache/ibus/bus/registry && systemctl --user restart
   org.freedesktop.IBus.session.GNOME`。

## 启用

```sh
gsettings set org.gnome.desktop.input-sources sources \
    "[('xkb', 'us'), ('ibus', 'qingjian')]"   # 或在设置里添加
ibus engine qingjian                          # 或 Super+Space 切换
```

server 必须在跑（`~/.local/qingjian/bin/qingjian-linux-server`，
配套 systemd 用户服务见 `../scripts/deploy-102.sh`）。server 不在时前端放行全部按键，
不影响正常打字。

## 测试

```sh
make test                # 样例词库（快）；make test DICT=real 用完整词库
```

不走 IBus、直连 server 验证协议链路（握手、逐键候选、译词、空格上屏），
server 由目标自动起停（独立 0700 socket 目录 + 临时 XDG）。

## 文件

| 文件 | 职责 |
|---|---|
| `src/main.c` | 进程入口：连 ibus-daemon、注册 factory、申请组件 DBus 名 |
| `src/engine.c` | IBusEngine 子类：握手、按键转发、帧渲染、轮询、候选点击 |
| `src/ipc.c/.h` | Unix socket + 长度前缀 JSON 帧收发 |
| `src/keymap.c/.h` | IBus keyval → 协议 KeyEvent（VK 与 fcitx5 插件一致） |
| `data/qingjian.component.in` | IBus 组件描述模板 |
| `tests/smoke_test.py` | 协议冒烟测试 |
