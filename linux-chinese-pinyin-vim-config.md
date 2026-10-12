# Linux 中文拼音输入法 + Vim 集成配置指南

> 适用于 Ubuntu / Debian GNOME 桌面环境，IBus + ibus-libpinyin 方案

## 概述

本文档介绍如何在 Linux (Ubuntu/Debian GNOME) 下配置中文拼音输入法，并集成 Vim 实现智能输入法切换：
- 进入插入模式自动切换中文拼音
- 按 ESC 退出插入模式自动切换回英文
- 同时支持系统词典和用户词典
- 用户词典可导出为纯文本方便迁移

## 环境要求

- Ubuntu 或 Debian 系统
- GNOME 桌面环境
- 已安装 IBus 和 ibus-libpinyin

检查是否已安装：
```bash
dpkg -l | grep -E "(ibus|libpinyin)"
```

如果未安装，先安装：
```bash
sudo apt update
sudo apt install ibus ibus-libpinyin
```

---

## 1. 配置 IBus 环境变量

编辑 `~/.xprofile` 文件，添加以下内容：

```bash
# IBus 输入法环境变量配置
export GTK_IM_MODULE=ibus
export QT_IM_MODULE=ibus
export XMODIFIERS=@im=ibus
```

**说明：**
- GNOME 在登录时会自动读取 `~/.xprofile`
- 如果你的系统使用 `~/.profile`，也可以添加到那里

使配置立即生效：
```bash
source ~/.xprofile
```

---

## 2. 配置 Vim 输入法自动切换

编辑 `~/.vimrc` 文件，在末尾添加：

```vim
" ========== IBus 输入法自动切换 ==========
" 进入插入模式 → 自动切换到中文拼音
autocmd InsertEnter * :silent !ibus engine pinyin
" 离开插入模式 (ESC) → 自动切换回英文
autocmd InsertLeave * :silent !ibus engine xkb:us::eng
```

验证配置语法是否正确：
```bash
vim -c "source ~/.vimrc" -c "q"
```
如果没有错误输出就是正确的。

---

## 3. 验证输入法引擎名称

运行以下命令确认 IBus 已安装的引擎：
```bash
ibus list-engine | grep -E "(pinyin|xkb)"
```

**预期输出：**
```
pinyin
xkb:us::eng
```

如果显示的拼音引擎名称不是 `pinyin`（比如显示 `libpinyin`），请将 `~/.vimrc` 中的 `pinyin` 替换为实际名称。

---

## 4. 词典配置（系统词典 + 用户词典）

### 打开 IBus 设置界面
```bash
ibus-setup
```

### 配置词典选项

1. 在 IBus 设置中选择 **Input Method** 标签
2. 选择 **Chinese (Pinyin)**
3. 点击 **Preferences** 进入 ibus-libpinyin 详细设置

在词典设置中，确保以下选项已启用：
- ✅ **系统词库** - 默认已启用，提供基础词库
- ✅ **用户词库** - 默认已启用，保存个性化词组
- ✅ **自动学习用户输入** - 自动学习输入习惯，调整词频

---

## 5. 用户词典维护与迁移

### 导出用户词典（备份/迁移前）

1. 打开 `ibus-setup` → 中文拼音设置
2. 选择 **User Dictionary** 标签
3. 点击 **Export** 按钮
4. 保存为纯文本文件，例如 `ibus-user-dict.txt`
5. 将文件备份到版本控制系统或其他存储位置

### 导入用户词典（新系统/恢复）

1. 打开 ibus-libpinyin 设置
2. 选择 **User Dictionary** → **Import**
3. 选择之前导出的文本文件
4. 重启 IBus 使导入生效

### 手动编辑用户词典

导出后的纯文本格式：**每行一个词组 + 拼音**，例如：

```
你好 ni hao
克劳德 ke lao de
输入法 shu ru fa
计算机 ji suan ji
```

可以直接编辑这个文本文件，然后重新导入即可。

### 用户词典存储位置

IBus-libpinyin 将用户词典存储在：
```
~/.config/ibus/libpinyin/user.db
```

---

## 6. 重启 IBus 使配置生效

```bash
ibus restart
```

---

## 7. 测试验证

### 手动测试输入法切换
```bash
# 切换到中文拼音
ibus engine pinyin
echo "Switched to pinyin"

# 切换到英文
ibus engine xkb:us::eng
echo "Switched to English"
```

两个命令都执行成功没有报错，说明配置正确。

### Vim 行为测试

| 操作 | 预期结果 |
|------|----------|
| 打开 Vim，normal 模式 | 输入法为英文 ✅ |
| 按 `i` 进入插入模式 | 自动切换到中文拼音 ✅ |
| 按 `ESC` 返回 normal 模式 | 自动切换回英文 ✅ |

---

## 常见问题

### Q: Vim 自动切换不工作怎么办？

A: 检查：
1. `ibus engine` 命令是否可以手动切换
2. 引擎名称是否正确 (`ibus list-engine` 确认)
3. IBus 服务是否运行：`pgrep ibus-daemon`

### Q: 环境变量不生效怎么办？

A: 需要注销重新登录，让 GNOME 重新读取 `.xprofile`。

### Q: 可以用 Fcitx5 代替 IBus 吗？

A: 可以。如果用 Fcitx5，Vim 配置需要改成：

```vim
" Fcitx5 自动切换
autocmd InsertEnter * :silent !fcitx5-remote -o
autocmd InsertLeave * :silent !fcitx5-remote -c
```

---

## 方案优点

- ✅ **GNOME 原生集成** - IBus 是 GNOME 默认输入法框架，集成度好
- ✅ **稳定可靠** - ibus-libpinyin 是成熟项目，维护活跃
- ✅ **系统词典 + 用户词典分离** - 基础词库稳定，个人词库可迁移
- ✅ **纯文本导出导入** - 用户词典方便备份、迁移、版本控制
- ✅ **Vim 智能切换** - ESC 退出自动切英文，提升使用体验

---

## 迁移 Checklist

换到新系统时：

1. [ ] 在新系统安装 `ibus ibus-libpinyin`
2. [ ] 配置 `~/.xprofile` 环境变量
3. [ ] 复制 `~/.vimrc` 中的自动切换配置
4. [ ] 导入之前导出的用户词典
5. [ ] 运行 `ibus restart`
6. [ ] 测试 Vim 自动切换功能

完成！
