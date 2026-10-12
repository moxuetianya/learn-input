# Linux 中文拼音输入法配置 + Vim 集成实现计划

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** 在 Ubuntu/Debian GNOME 环境下配置 IBus 中文拼音输入法，集成 Vim 自动输入法切换，同时支持系统词典和可迁移用户词典。

**Architecture:** 使用系统已安装的 IBus + ibus-libpinyin，配置环境变量，在 vimrc 中添加自动切换脚本，用户词典通过 IBus 导出/导入功能实现迁移。

**Tech Stack:** IBus, ibus-libpinyin, Vim

---

### Task 1: 配置 IBus 环境变量

**Files:**
- Create/Modify: `~/.xprofile`

- [ ] **Step 1: 检查现有 .xprofile 文件**

```bash
cat ~/.xprofile
```

- [ ] **Step 2: 添加 IBus 环境变量配置**

Add this content to `~/.xprofile`:

```bash
# IBus 输入法配置
export GTK_IM_MODULE=ibus
export QT_IM_MODULE=ibus
export XMODIFIERS=@im=ibus
```

- [ ] **Step 3: 使配置生效**

```bash
source ~/.xprofile
```

---

### Task 2: 配置 Vim 输入法自动切换

**Files:**
- Modify: `~/.vimrc`

- [ ] **Step 1: 添加自动切换 autocmd 脚本**

Add this to the end of `~/.vimrc`:

```vim
" IBus 输入法自动切换
" 进入插入模式自动切换到中文拼音
autocmd InsertEnter * :silent !ibus engine pinyin
" 离开插入模式自动切换到英文
autocmd InsertLeave * :silent !ibus engine xkb:us::eng
```

- [ ] **Step 2: 验证配置语法**

Start Vim and check for errors:

```vim
vim -c "source ~/.vimrc" -c "q"
```

Expected: no error output.

---

### Task 3: 验证输入法引擎名称

**Files:**
- 无文件修改，验证系统

- [ ] **Step 1: 检查 IBus 已安装的引擎**

```bash
ibus list-engine
```

Verify that `pinyin` and `xkb:us::eng` are listed. If `pinyin` has a different name, adjust in Task 2.

---

### Task 4: 配置 ibus-libpinyin 用户词典

**Files:**
- 无文件修改，配置说明

- [ ] **Step 1: 打开 IBus 设置**

```bash
ibus-setup
```

Go to "Input Method" → "Chinese (Pinyin)" → "Preferences"

- [ ] **Step 2: 配置词典选项**

在 ibus-libpinyin 设置中：
- 启用 "系统词库" (默认已启用)
- 启用 "用户词库" (默认已启用)
- 设置 "自动学习用户输入" 为开启

---

### Task 5: 用户词典导出/导入方法文档

**Files:**
- Create: `~/ibus-libpinyin-user-dict-export.md` (可选，说明文档)

- [ ] **Step 1: 创建用户词典维护说明文档**

```markdown
# IBus-libpinyin 用户词典维护指南

## 导出用户词典（迁移前）
1. 打开 ibus-setup → 中文拼音设置
2. 选择 "User Dictionary" → "Export"
3. 保存为纯文本文件 `user-export.txt`
4. 将此文件备份到版本控制系统或其他设备

## 导入用户词典（迁移后）
1. 同上打开设置
2. 选择 "User Dictionary" → "Import"
3. 选择之前导出的文本文件
4. 重启 IBus 生效

## 手动编辑用户词典
用户词典文件实际存储位置：
`~/.config/ibus/libpinyin/user.db`

导出后的纯文本格式：每行一个词 + 拼音，例如：
```
你好 ni hao
 Claude ke lai de
```

编辑后重新导入即可。

## 日常使用
- IBus 会自动学习你输入的词组，调整词频
- 新词会自动添加到用户词典
```

---

### Task 6: 重启 IBus 使配置生效

**Files:**
- 无文件修改

- [ ] **Step 1: 重启 IBus 服务**

```bash
ibus restart
```

- [ ] **Step 2: 验证输入法切换工作**

```bash
# 手动测试切换
ibus engine pinyin
ibus engine xkb:us::eng
```

Both commands should work without errors.
