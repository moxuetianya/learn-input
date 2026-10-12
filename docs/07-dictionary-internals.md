# 07 · 词库内幕：格式、源码与维护实践

> 输入法的"智能"来自三样东西：音节解析（拼音合法性）、**词典**（拼音→词
> 的映射）、**语言模型**（词频/上下文排序）。本章聚焦后两者，用本仓库
> `third_party/` 的源码拆解 libpinyin 与 libime 两套词库体系，回答那个
> 现实问题：**为什么 ibus-libpinyin 的词库不好维护，以及换 fcitx5 能好多少**。

## 1. 先看查词路径：词库在引擎里的位置

一次 `ni'hao` 查询在拼音引擎内部的形状（两套体系同构）：

```
输入串 "nihao"
  → 音节切分 (拼音合法性表: libpinyin pinyin_parser_table.h /
             libime pinyinencoder.cpp 的拼音树)
  → 得到切分方案 ["ni","hao"]
  → 词典查询: 拼音序列 → 候选词 token/词列表
     ├─ 系统词库 (只读, 上游发版)
     └─ 用户词库 (读写, 你的个人数据)
  → 语言模型排序: P(词|上文)  bigram/unigram 打分
     ├─ 系统模型 (bigram.db / *.lm)
     └─ 用户模型 (user_bigram / user.history)
  → 候选列表 + 记忆: 用户选中的词进入用户词典+用户模型
```

维护词库 = 维护右半边（用户词库 + 用户模型）。下面分别进入两套实现。

## 2. libpinyin 词库体系（ibus-libpinyin 背后）

### 2.1 系统数据（本机实测）

```
$ dpkg -L libpinyin-data    # Debian 打包位置
/usr/lib/x86_64-linux-gnu/libpinyin/data/
├── table.conf              ← 表清单与版本指纹（关键！见 2.3）
├── merged.bin              ← 主表（合并词库）
├── art.bin culture.bin economy.bin geology.bin history.bin
│   life.bin nature.bin people.bin science.bin society.bin
│   sport.bin technology.bin       ← 12 个"领域"分表（可按需加载）
├── pinyin_index.bin        ← 拼音→token 索引
├── phrase_index.bin        ← token→短语+词频 索引
├── bigram.db opengram.db   ← 系统二元模型（opengram 开放语料训练）
└── gb_char.bin gbk_char.bin ← 字集表
```

对应源码类型（`third_party/libpinyin/src/storage/`）：

| 数据结构 | 职责 | 源码 |
|---|---|---|
| `ChewingLargeTable2` | 拼音串→token（内存 chunk 数组 + 可选 BDB/KyotoCabinet/tkrzw 后端） | `chewing_large_table2.cpp` |
| `PhraseIndex`/`PhraseLargeTable3` | token↔短语、词频 | `phrase_index.cpp`, `phrase_large_table3.cpp` |
| `FlexibleSingleGram`/`Bigram` | ngram 项 | `flexible_single_gram.h`, `ngram.cpp` |

### 2.2 用户数据（`~/.config/ibus/libpinyin/`）

```c
// third_party/libpinyin/src/pinyin_internal.h —— 用户文件名硬编码
#define USER_TABLE_INFO     "user.conf"               // 元信息
#define USER_BIGRAM         "user_bigram.db"          // 用户二元模型
#define USER_PINYIN_INDEX   "user_pinyin_index.bin"   // 用户拼音索引(内存快照)
#define USER_PHRASE_INDEX   "user_phrase_index.bin"   // 用户短语索引(内存快照)
```

加载流程（`third_party/libpinyin/src/pinyin.cpp` 173–213 行，走读要点）：

1. 读 `user.conf`（`UserTableInfo`）；
2. **`user_table_info.is_conform(&m_system_table_info)` 校验用户数据与
   系统表指纹是否一致**；不一致则 `make_conform` —— **用户词库作废重来**；
3. 依次加载 user 索引与 bigram，系统表按 `table.conf` 清单挂载。

### 2.3 痛点的源码级解释：为什么"不好维护"

1. **格式是私有二进制**：`user_pinyin_index.bin` 等是 `PhraseIndex` 内存
   结构的 dump（token 内联了 `-log P` 定点词频），无格式文档、跨版本无
   稳定承诺。`user_bigram.db` 是 `FlexibleBigram` 的私有封装；
2. **与系统表强耦合**：`user.conf` 记录系统表指纹（MD5 与选项），发行版
   升级 libpinyin-data（词库微调、新增领域表）都可能触发 `make_conform`
   ——你积累的用户词**静默清零**（2.2 走读的分支）；
3. **导入导出只有窄门**：唯一官方出口是 ibus-libpinyin 设置界面的
   Export/Import。实现见 `third_party/ibus-libpinyin/src/PYLibPinyin.cc`：
   ```cpp
   // exportPinyinDictionary(): 把用户短语表+用户 bigram 迭代导出为文本
   export_iterator_t *iter =
       pinyin_begin_get_phrases(m_pinyin_context, USER_DICTIONARY);
   while (pinyin_iterator_has_next_phrase(iter)) {
       pinyin_iterator_get_next_phrase(iter, &phrase, &pinyin, &count);
       fprintf(dictfile, "%s %s %d\n", phrase, pinyin, count); // 词 拼音 词频
   }
   ```
   限制：只覆盖用户词表+bigram；**词频数值是相对次数**，导入时与现有词
   频如何合并未定义——两台机器的 user.db **没有合并语义**（只能整份替换
   或追加，追加后词频互相污染）；
4. **无工具链**：没有 CLI，没有增量同步，没有文本 diff 能力；GUI 一键
   导出是你唯一的备份手段（脚本化要靠模拟点击或解析二进制）。

> 结论一句话：libpinyin 把用户数据当作**缓存**设计（可丢弃、可重建），
> 而用户把它当**资产**使用——预期错位是维护痛苦的根源。

### 2.4 能做的缓解（若暂时留在 ibus-libpinyin）

- 定期 GUI 导出文本（`词 拼音 次数` 三列，可 diff/可 grep），纳入 git；
- 换机/升级前先导出；升级后若词库清零，重新导入文本；
- 不要直接拷 `~/.config/ibus/libpinyin/` 做迁移（带指纹，跨版本易失效）。

## 3. libime / fcitx5 词库体系

### 3.1 系统词库：格式文档化的二进制

fcitx5 拼音引擎（fcitx5-chinese-addons）的算法库是 libime，词库分三层：

```
/usr/share/libime/                       系统词库（发行版打包）
~/.local/share/fcitx5/pinyin/dictionaries/  额外语料（*.dict, GUI 词库管理可加）
~/.local/share/fcitx5/pinyin/user.dict     用户词库（自动学习）
~/.local/share/fcitx5/pinyin/user.history  用户语言模型（整句记忆）
```

二进制格式（`PinyinDictionary : TrieDictionary`，`pinyindictionary.cpp`）：
double-array trie 打包 + zstd 压缩，带版本头（V1/V2），但**配套的文本格式
是官方文档**（`third_party/libime/src/libime/pinyin/
pinyindictionary-text-format_zh.md`）：

```text
<汉字> <全拼> [<权重>]
你好 ni'hao 1.5
小企鹅 xiao'qi'e
```

（拼音用 `'` 分隔音节；支持引号转义含空白的词条。）

### 3.2 工具链：文本与二进制双向门

```
third_party/libime/tools/
├── libime_pinyindict    # 拼音词库  text ↔ binary 互转（核心工具）
├── libime_tabledict     # 码表类（五笔等）同等待遇
├── libime_history       # 语言模型导出/统计
└── libime_migrate_fcitx4_*  # 从 fcitx4 迁移
```

典型工作流（全部命令行、可脚本化、可版本控制）：

```bash
# 导出二进制 → 文本
libime_pinyindict /usr/share/libime/pinyin.dict my.txt
# 编辑 my.txt（每行: 词 拼音 权重），git diff 看变化
# 文本 → 二进制（自建词库/修正词库）
libime_pinyindict my.txt my.dict
# 放进额外词库目录即可被引擎加载
```

### 3.3 用户数据：engine 源码走读

保存路径（`third_party/fcitx5-chinese-addons/im/pinyin/pinyin.cpp` 2443 行起，
`PinyinEngine::save()`）：

```cpp
standardPath.safeSave(PkgData, "pinyin/user.dict", [this](int fd) {
    ime_->dict()->save(libime::PinyinDictionary::UserDict, out,
                       libime::PinyinDictFormat::Binary);   // 用户词表
});
standardPath.safeSave(PkgData, "pinyin/user.history", [this](int fd) {
    ime_->model()->save(out);                               // UserLanguageModel
});
```

- `user.dict`：用户词 + 词频（同一个 TrieDictionary 格式，**导出工具直接
  能读**——`libime_pinyindict ~/.local/share/fcitx5/pinyin/user.dict u.txt`）；
- `user.history`：`UserLanguageModel`（`core/userlanguagemodel.cpp`，基于
  `HistoryBigram`）——记忆整句上下文的转移概率；
- **没有系统表指纹耦合**：系统词库升级不废用户数据（格式独立版本化）；
- 词库 GUI（fcitx5-chinese-addons 的 pinyindictmanager）只是工具链的
  壳子，底层同 CLI。

### 3.4 生态数据：词库是活的

- **fcitx5-pinyin-zhwiki**：从中文维基百科自动抽取词条生成的词库，跟随
  维基更新定期发版（社区维护的"热词来源"）；另有萌娘百科词库；
- opencc 做繁简校对后入库；
- 对比 libpinyin 的系统词库：随 libpinyin-data 版本发布，更新频率低得多。

## 4. 两套体系对照表

| 维度 | libpinyin (ibus) | libime (fcitx5) |
|---|---|---|
| 文本格式 | 仅导出用户词（GUI 触发）三列文本，无规范文档 | **官方文档化**（text-format md），含转义规则 |
| 二进制格式 | 私有内存 dump + 指纹耦合 | 版本化 trie+zstd，工具可读写 |
| CLI 工具 | 无 | `libime_pinyindict` 等全套 |
| 用户数据升级兼容 | 系统表指纹不符即弃 | 独立版本化，升级不丢 |
| 合并两份用户数据 | 无语义 | 文本导出→并集去重→重打包（社区惯例） |
| 热词数据源 | 发行版包 | zhwiki 等持续更新包 + 自建 |
| 脚本化/CI | 无 | 全链路可脚本 |
| 用户模型导出 | bigram 文本（次数相对值） | `libime_history` 统计/导出 |

顺带一提第三个生态位——**Rime**（中州韵）：词库即纯文本配置（YAML+Lua，
"八股文"词库如 rime-ice 可 git 管理），可维护性最强、定制自由度最高，
代价是配置门槛高、默认体验需调教。它通过 ibus-rime/fcitx5-rime 挂进两大
框架。若你的核心诉求是"词库完全自主"，09 章选型会把三个选项摆在一起。

## 5. 维护实践 Recipes（fcitx5 路线）

```bash
# 备份（可入 git）：
cp -r ~/.local/share/fcitx5/pinyin ~/backup-pinyin-$(date +%F)
libime_pinyindict ~/.local/share/fcitx5/pinyin/user.dict user-dict.txt

# 迁移到新机器：
# 整个 pinyin/ 目录拷走即可（user.dict + user.history 无系统耦合）

# 合并两台机器的词库（文本并集 + 词频取 max）：
libime_pinyindict A/user.dict a.txt && libime_pinyindict B/user.dict b.txt
python3 merge.py a.txt b.txt > merged.txt     # 按 (词,拼音) 键合并
libime_pinyindict merged.txt merged.dict
# 替换 user.dict 后重启 fcitx5 (fcitx5-remote -r? 实为退出重登/ fcitx5 -rd)

# 自建领域词库（例：把项目术语表变成词库）：
# terms.tsv (术语<TAB>拼音) → 转空格分隔 + 拼音加 ' → libime_pinyindict 打包
# 放入 ~/.local/share/fcitx5/pinyin/dictionaries/（GUI 词库管理也能添加）
```

## 6. 本章小结

- 词库体系 = 词典（映射）+ 语言模型（排序），用户数据是两套都要维护的资产；
- libpinyin 的用户数据设计为"可丢弃缓存"：私有二进制、系统表指纹耦合、
  无合并语义、无 CLI——你的维护痛点是结构性的，不是使用姿势问题；
- libime 把文本格式当一等公民：文档化 + `libime_pinyindict` 双向工具 +
  无耦合升级 + 活跃数据生态——词库维护从"黑盒祈祷"变成"git 工作流"；
- Rime 是"词库即配置"的第三条路，自由度最高、门槛也最高。

选型与贡献路径：[09-contribution.md](09-contribution.md)
