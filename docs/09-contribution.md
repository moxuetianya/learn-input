# 09 · 参与开源输入法项目：选型与上手路径

> 终章回答你的原始问题："后续我选一个具体的方案，可能参与到维护开源输入法
> 的项目中"。先给出全景与量化对比，然后给出我的选型建议和一条可执行的上手路径。

## 1. 相关项目全景（以本仓库克隆时点 2026-09 计）

| 项目 | 语言/构建 | 代码量(实测) | 活跃度 | 在本仓库 |
|---|---|---|---|---|
| ibus | C/GObject + autotools | ~76k 行 | 持续维护（GNOME 生态绑定） | `third_party/ibus` |
| ibus-libpinyin | C/C++ + autotools + GTK | ~28k 行 | 维护中（随 ibus 节奏） | `third_party/ibus-libpinyin` |
| libpinyin | C/C++ 双构建(autoconf+cmake) | ~58k 行 | 维护中（数据更新为主） | `third_party/libpinyin` |
| fcitx5 | C++ + CMake | ~99k 行 | 非常活跃 | `third_party/fcitx5` |
| libime | C++ + CMake | ~22k 行 | 活跃（fcitx5 算法库） | `third_party/libime` |
| fcitx5-chinese-addons | C++ + CMake | ~16k 行 | 非常活跃（拼音/五笔等） | `third_party/fcitx5-chinese-addons` |
| librime (+ 各前端) | C++ + CMake | （未克隆） | 活跃，词库生态繁荣 | — |
| fcitx5-pinyin-zhwiki 等 | 数据仓库（生成式） | — | 高频更新 | — |

结构性判断：

- **ibus 侧**是 Red Hat/GNOME 基础设施，改动谨慎、评审周期长，外部贡献者
  需要适应 GNOME 流程（gitlab.gnome.org / freedesktop）；
- **fcitx5 侧**是社区驱动、模块化仓库群，词库工具链（libime）与数据项目
  （zhwiki 词库）天然适合"小步快跑"型贡献；
- **libpinyin** 的引擎算法（ngram、智能拼音）有学术背景，代码注释详尽，
  但外围工具（词库管理）正是它的短板——也是潜在贡献点，只是用户基数
  决定回报面窄。

## 2. 从源码构建（本机 Debian 13 实操）

### 2.1 准备构建依赖

```bash
# 方法一：让 apt 装齐打包依赖（需先启用 deb-src 源）
sudo sed -i 's/^Types: deb$/Types: deb deb-src/' /etc/apt/sources.list.d/*.sources
sudo apt update
sudo apt build-dep fcitx5 fcitx5-chinese-addons libpinyin ibus ibus-libpinyin

# 方法二：手动装核心依赖（够跑大部分构建）
sudo apt install build-essential cmake ninja-build extra-cmake-modules \
     gettext pkg-config libxkbcommon-dev libglib2.0-dev \
     libexpat1-dev libfmt-dev libxcb-dev wayland-protocols \
     libevent-dev libzstd-dev libboost-dev?   # 视报错补齐
```

### 2.2 构建并跑测试

```bash
# fcitx5（CMake 标准流程）
cd third_party/fcitx5
cmake -B build -DCMAKE_BUILD_TYPE=Debug -DENABLE_TEST=ON
cmake --build build -j$(nproc)
ctest --test-dir build --output-on-failure | tail -5

# libime / fcitx5-chinese-addons 同样姿势
# libpinyin（双构建，走 cmake 较省事）
cd ../libpinyin && cmake -B build && cmake --build build -j$(nproc)

# ibus-libpinyin（autotools）
cd ../ibus-libpinyin && ./autogen.sh --prefix=/tmp/prefix && make -j$(nproc)
```

验证改动最短路：很多词库/引擎行为有单测兜底（fcitx5-chinese-addons
的 `test/`、libime 的 `test/testpinyindictionary.cpp`）——**先跑通测试再改**，
是这类项目 PR 的默认预期。

### 2.3 第一次"Hello IME"改动（半小时体验）

```bash
# 给 libime 拼音词库文本加一个自造词并验证:
cd third_party/libime
echo "深浅输入法 shen'qian'shu'ru'fa 2.0" >> /tmp/my.txt
./build/libime_pinyindict /tmp/my.txt /tmp/my.dict   # 文本→二进制
./build/libime_pinyindict /tmp/my.dict /tmp/roundtrip.txt  # 反向验证
diff /tmp/my.txt /tmp/roundtrip.txt && echo "词库打包链路 OK"
```

## 3. 上手贡献的四级阶梯

| 级别 | 内容 | 典型任务 | 难度 |
|---|---|---|---|
| L0 数据/词库 | 词表增补、错词修正、zhwiki 生成脚本 | fcitx5-pinyin-zhwiki PR；libpinyin 词表 issue | ★ |
| L1 工具链 | CLI 改进、格式转换器、迁移脚本 | libime tools；写 ibus→fcitx5 词库迁移器 | ★★ |
| L2 引擎行为 | 候选排序、快捷键、UI 文案与 i18n | chinese-addons issues；fcitx5 UI addon | ★★★ |
| L3 核心算法 | 解码器、Ngram、协议实现 | libime lattice/decoder；waylandim | ★★★★ |

建议从 **L0/L1 起步**：词库 PR 审核快、影响立竿见影，还能倒逼你读完
07 章的格式知识；等对代码布局熟了再进 L2。

沟通渠道以各仓库 README/CONTRIBUTING 为准（fcitx 系有 Telegram/Matrix
中文社区；ibus 走 GNOME GitLab issue/MR）。

## 4. 方案选型建议（结合你的两个目标）

你的目标：(a) 词库可维护、可迁移、可版本化；(b) 有朝一日参与维护。

### 方案 A：留守 ibus + libpinyin
- 优点：GNOME 零折腾；
- 缺点：词库维护痛点是**结构性**的（07 章 2.3 的四条源码证据），个人努力
  无法绕过；贡献面窄。
- 结论：仅当"完全不折腾"权重最高时选。

### 方案 B：转 fcitx5 生态（fcitx5 + libime + chinese-addons）★ 推荐
- 词库：文本格式一等公民 + CLI 全链路，git/diff/CI 全部适用（07 章 3.x）；
- 桌面：Debian 13 的 GNOME 可用（05 章切换步骤），KDE/wlroots 原生；
- 贡献：现代 C++、模块化仓库、活跃数据生态，L0–L3 阶梯都有落点；
- 风险：GNOME 下的集成细节需要调教（候选窗风格、切换键）。

### 方案 C：Rime（fcitx5-rime / ibus-rime 挂载）
- 词库即文本配置（YAML），rime-ice 等八股文生态可直接 git 管理，定制
  自由度最高；
- 贡献：词库生态活跃，但引擎层门槛高，配置体系学习曲线陡；
- 结论：若你享受"输入法即编程"，C 是长期玩具；**日用 B、折腾 C 不冲突**
  （fcitx5 里可随时切 rime 引擎）。

### 我的建议路径
1. 本机启用 fcitx5（`im-config -n fcitx5`，05 章 5 节）日用两周；
2. 用迁移脚本（下节）把 ibus-libpinyin 用户词搬过去；
3. 每周把 `user.dict` 导出文本进 git，体验工作流差异；
4. 从给 libime 或 zhwiki 词库提一个小 PR 开始（L0），两个月内自然知道
   自己想不想深入 L2/L3。

## 5. 交付：ibus-libpinyin → fcitx5 词库迁移脚本

仓库提供 `scripts/ibus2fcitx5.py`：读取 ibus-libpinyin 导出的用户词库文本，
转成 libime 文本词库格式（可直接 `libime_pinyindict` 打包）。

```bash
# 1) ibus-libpinyin 设置界面导出用户词库 → user_export.txt
#    （格式: "词 拼音空格分隔 [次数]"，见 07 章 2.3 源码）
python3 scripts/ibus2fcitx5.py user_export.txt > my-history.txt
# 2) 打包成 fcitx5 用户词库
libime_pinyindict my-history.txt ~/.local/share/fcitx5/pinyin/user.dict
# 3) 重启 fcitx5 生效（fcitx5-remote -r 或注销重登）
```

格式差异说明（脚本的全部逻辑）：

| | ibus-libpinyin 导出 | fcitx5 文本格式 |
|---|---|---|
| 分隔 | 空格分隔音节 `ni hao` | `'` 连接 `ni'hao` |
| 词频 | 整数次数（可省略） | 浮点权重（可省略） |
| 轻声/儿化 | 已切好音节 | 原样保留 |

## 6. 全教程回顾

| 章 | 你获得了什么 |
|---|---|
| 01 | 分层地图与四条链路 |
| 02 | 内核侧终点：evdev/uinput 的机制与边界 |
| 03 | 基线：libinput/XKB/wl_keyboard 如何产字 |
| 04 | 协议森林：text-input×input-method 与桌面矩阵 |
| 05 | IBus vs Fcitx5 架构对比与切换实操 |
| 06 | 五条实路布线 + 按键消费顺序 |
| 07 | 词库格式源码级对比与维护工作流 |
| 08 | 分层调试方法论与故障树 |
| 09 | 本章：选型与贡献路径 |
| 10 | 亲手做一个：qingjian 的 IBus 前端，从协议到部署 |

从按键到词库，整条链路的所有"黑盒"都已打开。剩下的深度，去
`third_party/` 的源码和上游 issue 里挖吧——或者直接读本仓库的实践产出
[ibus-qingjian/](../ibus-qingjian/)（配套 [10-ibus-qingjian.md](10-ibus-qingjian.md)），
那里有一条从零到能用的完整路径。
