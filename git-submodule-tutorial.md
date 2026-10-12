# 青简（qingjian）输入法分析 & git submodule absorbgitdirs 实操教程

> 本文基于本机真实环境验证：git 2.43.0，验证日期 2026-10-11。
> 所有命令与输出都在临时目录里实际跑通过，可放心照做。

---

## 一、qingjian 输入法分析

### 1.1 一句话简介

**青简**是一款 Rust 编写的跨平台中文拼音输入法，GPL-3.0-or-later 开源，
口号是「好好输入，顺便多认识一个词」——你正常打字，每个候选词旁边显示一条
**译词**（英语 / 日语 / 西班牙语可选），让语言学习自然融入日常输入。

- 上游仓库：<https://github.com/qingjian-team/qingjian>
- 本地克隆版本：`4a3e963`（`chore(release): 开发版 0.2.1-dev`）
- 支持平台：macOS 13+（IMK）、Windows 10/11（TSF）、Linux（Fcitx5 插件 + Rust Server）

### 1.2 架构：Core 平台无关，各平台只做壳

```text
                    Qingjian Core
                         │
        ┌────────────────┼────────────────┐
        ▼                ▼                ▼
   macOS Adapter    Windows Adapter    Linux Adapter
       IMK               TSF          IBus / Fcitx5
        │                │                │
        ▼                ▼                ▼
   Candidate UI      Candidate UI      Candidate UI
```

三条架构铁律（见 `docs/design/architecture.md`）：

1. **Core 平台无关**——`crates/` 下任何 crate 不许碰平台 API；换掉 IMK 不应该改 Core 一行代码。
2. **一个候选词只显示一种辅助语言**——翻译是候选的可选 annotation，不是第二套候选系统。
3. **输入优先于学习**——翻译查询绝不能阻塞候选生成。

### 1.3 Workspace 组成

| 位置 | 职责 |
|---|---|
| `crates/qingjian-core` | 引擎本体，`Engine` 是唯一门面：拼音解析、纠错、候选、排序、整句、双拼、注音、英文模式 |
| `crates/qingjian-dictionary` | 词库（TSV 或 `.qj` mmap），按音节位置二分查询 |
| `crates/qingjian-translate` | 释义表与词汇等级表（候选旁那条译词的来源） |
| `crates/qingjian-learning` | 用户侧落盘：词频 / 用户词 / 个人 n-gram / 敲错表 / 输入日志 / 统计 / 生词本 |
| `crates/qingjian-predict` | 云联想（OpenAI 兼容接口，**默认关闭**） |
| `crates/qingjian-lm` | 整句转换的 bigram 语言模型 |
| `crates/qingjian-neural` | 字级 Transformer 本地推理（candle），给整句路径重打分 |
| `crates/qingjian-format` | `.qj` 数据容器：mmap 读、零拷贝、哈希索引 |
| `crates/qingjian-render` | 自绘渲染器：候选窗一帧 + 主题 → 位图，各平台只贴图 |
| `crates/qingjian-update` | 检查更新：读 `releases.json`、验 ed25519 签名，只提示不安装 |
| `apps/{macos,windows,linux}` | 三个平台壳：IMK / TSF DLL + Server + WinUI 设置 / Fcitx5 + Server |
| `apps/cli` | Core 的验证工具：查询、逐键计时、日志回放评测、常数扫描 |
| `tools/*` | 产品数据生成（词库 / 释义 / 语料）与发版签名 |

### 1.4 特点小结

- **本地优先**：拼音转换、词库、模型、学习、统计全在本机；无账号、不上传。
- **整句输入**：bigram 语言模型 + 本地神经网络重打分，停顿后自动调整句子候选。
- **输入方式全**：全拼、简拼、双拼、纠错、辅码，五笔在路线图中。
- **文档工程化**：`docs/` 分 design / plan / notes / user 四类，约定「文档写中文、标识符英文」。

> 完整的仓库约定见 `qingjian/CLAUDE.md` 与 `qingjian/docs/contributing.md`。

---

## 二、场景与目标

当前目录的真实状态（`git status` 已确认）：

```text
learn-input/                  ← 不是 git 仓库
└── qingjian/                 ← 完整克隆，自带 .git/ 目录
    └── .git                  ← 目录（不是文件），origin 指向 GitHub
```

**目标**：把 `learn-input` 变成父仓库，把已经克隆好的 `qingjian` 收编为它的**子模块**。
要求：不重新克隆、不丢历史、`.git` 不再躺在 `qingjian/` 肚子里——这正是
`git submodule absorbgitdirs` 的用武之地。

---

## 三、absorbgitdirs 是什么

`git help submodule` 里的原文语义：

> 如果某个子模块的 git 目录在子模块自己内部，就把它**搬进**父仓库的
> `.git/modules/<name>/`，然后在原地放一个 `.git` **文件**（内容为
> `gitdir: <路径>`），把工作树重新接回父仓库里的 git 目录。

为什么需要它：

- **正常流程**（父仓库 → `git submodule add URL`）中，git 会自己克隆子模块，
  git 目录天然就放在父仓库的 `.git/modules/` 里，用不着 absorbgitdirs。
- **本场景**（先手动克隆、后注册）中，`qingjian/.git` 还是一个完整的目录。
  `git submodule add` 只会登记它、**不会搬它**。absorbgitdirs 负责最后这一步「收编」。

搬完之后的效果：

```text
搬家前                                搬家后
learn-input/                         learn-input/
├── .git/          (父仓库)          ├── .git/
└── qingjian/                          │   └── modules/qingjian/   ← 子模块真正的对象库
    └── .git/        ← 完整目录        └── qingjian/
                                         └── .git               ← 变成文件: gitdir: ../.git/modules/qingjian
```

### 三个必踩的坑（均已实测）

| 坑 | 现象 | 解法 |
|---|---|---|
| ① 命令名是**复数** | `git submodule absorbgitdir qingjian`（没有 s）只打印用法说明然后退出码 1 | 写全：`git submodule absorbgitdirs` |
| ② 先 `git add` 过嵌入式仓库 | 出现 `warning: 正在添加嵌入式 git 仓库`，索引里记一条 `160000` gitlink；之后再 `submodule add` 报 `fatal: 'qingjian' 已经存在于索引中` | 先 `git rm -f --cached qingjian`（注意要 `-f`，否则报「暂存的内容和工作区及 HEAD 中的都不一样」） |
| ③ `.gitmodules` 里写了本地路径 | 新克隆者执行子模块克隆时被安全策略拦截：`fatal: 传输 'file' 不允许`（CVE-2022-39253 缓解措施） | `.gitmodules` 一律写官方 URL；确需本地路径时设 `git config --global protocol.file.allow always` |

---

## 四、实操：把 qingjian 收编为子模块

在 `learn-input` 目录下按顺序执行（每一步的预期输出都标出来了）：

### 第 1 步：初始化父仓库

```bash
cd /home/peter/project/learn/learn-input
git init -b main
git commit --allow-empty -m "init"    # 先有一个提交，后续操作更稳
```

### 第 2 步：如果之前 `git add` 过 qingjian，先撤销

```bash
git rm -f --cached qingjian
# 报「pathspec 'qingjian' 未匹配任何文件」说明本来就没加过，忽略即可
# 只动索引，qingjian/ 目录和里面的文件一个都不会少
```

### 第 3 步：注册为子模块（不重新克隆！）

```bash
git submodule add https://github.com/qingjian-team/qingjian qingjian
```

预期输出：

```text
向索引中添加位于 'qingjian' 的已存在的仓库
```

git 发现 `qingjian/` 已经是同一个仓库（origin 与 URL 一致），直接登记，**不需要网络**。
此时生成 `.gitmodules`：

```ini
[submodule "qingjian"]
	path = qingjian
	url = https://github.com/qingjian-team/qingjian
```

> 注意：这一步之后 `qingjian/.git` **仍然是目录**，`.git/modules` 还不存在——收编要靠下一步。

### 第 4 步：关键一步——吸收 git 目录

```bash
git submodule absorbgitdirs qingjian
```

预期输出：

```text
将 'qingjian' 的 git 目录从
'/home/peter/project/learn/learn-input/qingjian/.git' 迁移至
'/home/peter/project/learn/learn-input/.git/modules/qingjian'
```

### 第 5 步：提交

```bash
git add .gitmodules qingjian
git commit -m "build: 以子模块方式引入 qingjian"
```

### 验证

```bash
cat qingjian/.git
# gitdir: ../.git/modules/qingjian          ← .git 已变成一个指针文件

git submodule status
# 4a3e963dfe299126574bfe3c370ff70a3f85ee77 qingjian (v0.2.0)   ← 记录的是精确 commit

git -C qingjian log --oneline -1
# 4a3e963 chore(release): 开发版 0.2.1-dev                      ← 历史完好无损

ls .git/modules/
# qingjian                                                  ← 对象库搬进来了
```

### 如何回退（absorbgitdirs 完全可逆）

```bash
# 把 git 目录搬回去，恢复成普通独立仓库
mv .git/modules/qingjian qingjian/.git
git config --remove-section submodule.qingjian
rm .gitmodules
git rm --cached qingjian
```

---

## 五、收编之后的日常用法

### 5.1 新机器上克隆父仓库

```bash
git clone --recurse-submodules <learn-input 的仓库地址>
# 或者已经普通克隆过了，补一步：
git submodule update --init --recursive
```

### 5.2 跟进上游更新

```bash
# 把 qingjian 拉到上游默认分支最新，然后父仓库记录新位置
git submodule update --remote qingjian
git add qingjian
git commit -m "build: qingjian 更新至 <新 commit>"
```

### 5.3 固定到某个版本

```bash
git -C qingjian checkout v0.2.0
git add qingjian && git commit -m "build: qingjian 锁定 v0.2.0"
```

### 5.4 在子模块里开发

```bash
cd qingjian
git switch -c my-feature        # 重要！子模块检出后默认是 detached HEAD，属正常现象
# ……改代码、提交、push 到自己的 fork……
# 然后回到父仓库 git add qingjian，把父仓库指向你的新 commit
```

### 5.5 彻底移除子模块

```bash
git submodule deinit -f qingjian     # 清掉工作区里的检出
git rm -f qingjian                   # 删 gitlink 与工作树
rm -rf .git/modules/qingjian         # 手动清掉对象库（git rm 不会删这里）
```

---

## 六、原理速查

| 概念 | 存在哪 | 内容 |
|---|---|---|
| gitlink（模式 `160000`） | 父仓库的提交对象里 | 只是一条「qingjian 在这个 commit」的指针，不含任何文件内容 |
| `.gitmodules` | 父仓库工作树（纳入版本控制） | `path` 与 `url` 的映射，告诉克隆者去哪拿子模块 |
| `.git/modules/<name>/` | 父仓库 `.git` 内部 | 子模块真正的对象库、引用与配置 |
| `qingjian/.git`（文件） | 子模块工作树 | 一行 `gitdir: ../.git/modules/qingjian`，把工作树指回上面的对象库 |

一句话总结：**`git submodule add` 负责「登记」，`absorbgitdirs` 负责「搬家」**——
对先克隆后注册的仓库，两步缺一不可。

---

## 七、附：合并两条无关联历史（--allow-unrelated-histories）

> 本节内容于 2026-10-12 在本仓库真实操作验证：把 `origin/master`（旧仓库
> `feat: init`，单独 init 的独立历史）合并进本地 `main`（教程 + qingjian 子模块）。

### 7.1 适用场景

两个**分别 `git init`、没有共同祖先**的仓库要合成一条历史，且**双方的提交都要保留**。
典型情况：本地已经开了一套新历史（如本仓库），远端还有一个更早的旧仓库，想把旧内容并进来。

### 7.2 标准流程（以 A 为主仓库，合并 B）

```bash
# 在 A 仓库中
git remote add other /path/to/B        # 或 B 的 remote 地址；已有 origin 就跳过
git fetch other

# 合并 B 的历史，允许无关联
git merge other/master --allow-unrelated-histories

# 用完删掉临时 remote
git remote remove other
```

成功后 `git log --graph --oneline` 会出现一个**双根**的 merge commit：

```text
*   f67f4d6 merge: 合并 origin/master 的无关联历史（learn-input 旧仓库 init）
|\
| * 93f2f7c feat: init                        ← B 的根
* 613c779 build: 以子模块方式引入 qingjian      ← A 的历史
* 3a33b15 docs: qingjian 分析与 submodule absorbgitdirs 教程
```

### 7.3 有冲突时

两边都有同名文件（如各自的 `README.md`）就会冲突，解决后**只 add 具体路径**再提交：

```bash
git status --short        # AA = 两边都新增了同名文件
# ……手动编辑冲突文件……
git add README.md         # 不要无脑 git add . —— 会把工作区无关的未跟踪文件一并暂存
git commit                # 什么都不用 -m，git 会自动填好 "Merge ..." 结束合并
```

无冲突时 merge 直接完成，不需要手动 commit。

### 7.4 版本说明（常见误区）

- `--allow-unrelated-histories` 是 **Git 2.9（2016）新增**的参数；
- **2.9 之前的旧版本默认就允许**合并无关联历史，不需要任何参数或提前 pull——
  「老版本要先 git pull 或改配置」的说法是**反的**；
- 2.9 起出于安全改为默认拒绝，必须显式带此参数。

### 7.5 与子模块的相互影响（本仓库实录）

- 合并对已登记的子模块（`qingjian` + `.gitmodules`）**无影响**，`git submodule status` 照常工作；
- 但旧仓库里如果存在**没有 `.gitmodules` 登记的裸 gitlink**（`git add` 嵌入式仓库的产物，
  即坑②的形态——本仓库 `third_party/` 下的 6 个目录就是），合并后它们原样保留，
  `git submodule status` 会报错：
  `fatal: 在 .gitmodules 中没有发现路径 'third_party/fcitx5' 的子模组映射`。
- 想收编这些裸 gitlink：`git submodule add <上游URL> third_party/<名字>` 后
  `git checkout <原 gitlink 记录的 hash>` 固定回原版本，再提交。
