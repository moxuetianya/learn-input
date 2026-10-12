#!/usr/bin/env python3
"""ibus-libpinyin 用户词库 → fcitx5 (libime) 文本词库格式转换器。

背景:
  ibus-libpinyin 设置界面 Export 导出的用户词库为文本(见教程 docs/07):
      <词> <拼音音节以空格分隔> [<使用次数>]
      例: 你好 ni hao 3
  fcitx5 的 libime 文本词库格式(上游文档 pinyindictionary-text-format):
      <词> <全拼以 ' 连接> [<权重>]
      例: 你好 ni'hao 3

用法:
  python3 ibus2fcitx5.py user_export.txt > my-history.txt
  python3 ibus2fcitx5.py --test        # 内置样例自测

之后:
  libime_pinyindict my-history.txt ~/.local/share/fcitx5/pinyin/user.dict
"""
import re
import sys

SYLLABLE = re.compile(r"^[a-z]+[0-9]?$")  # 音节(结尾可能带轻声/变调标记数字)


def convert_line(line: str):
    """返回 (fcitx5行 or None, 备注)"""
    line = line.strip()
    if not line:
        return None, None
    tokens = line.split()
    if len(tokens) < 2:
        return None, f"跳过(字段不足): {line!r}"

    phrase = tokens[0]
    # 末字段若为纯整数则为使用次数
    count = None
    if len(tokens) >= 3 and tokens[-1].isdigit():
        count = tokens[-1]
        syllables = tokens[1:-1]
    else:
        syllables = tokens[1:]

    # 校验音节形状, 防御异常行(如导出首尾说明、乱码)
    for s in syllables:
        if not SYLLABLE.match(s):
            return None, f"跳过(非拼音音节 {s!r}): {line!r}"
    if not phrase or any(c.isspace() for c in phrase):
        return None, f"跳过(词含空白): {line!r}"

    pinyin = "'".join(syllables)
    return (f"{phrase} {pinyin} {count}" if count else f"{phrase} {pinyin}"), None


def convert(stream_in, stream_out, stream_err):
    n_ok = n_skip = 0
    for line in stream_in:
        out, note = convert_line(line)
        if out is not None:
            stream_out.write(out + "\n")
            n_ok += 1
        elif note:
            stream_err.write(note + "\n")
            n_skip += 1
    return n_ok, n_skip


def selftest():
    sample = [
        "你好 ni hao 3",          # 常规: 词+双音节+次数
        "计算机 ji suan ji",       # 无次数
        "克劳德 ke lao de 12",
        "",                        # 空行
        "# 注释行被跳过",           # 非拼音音节
        "单字 a",                  # 单音节
        "异常行",                  # 字段不足
    ]
    expect_ok = [
        "你好 ni'hao 3",
        "计算机 ji'suan'ji",
        "克劳德 ke'lao'de 12",
        "单字 a",
    ]
    import io
    out, err = io.StringIO(), io.StringIO()
    n_ok, n_skip = convert(sample, out, err)
    got = [l for l in out.getvalue().splitlines() if l]
    assert got == expect_ok, f"结果不符:\n{got}\nvs\n{expect_ok}"
    assert n_ok == 4 and n_skip == 2  # 空行不计入跳过
    print("selftest OK:")
    print("\n".join("  " + g for g in got))


if __name__ == "__main__":
    if len(sys.argv) == 2 and sys.argv[1] == "--test":
        selftest()
        sys.exit(0)
    if len(sys.argv) != 2:
        print(__doc__)
        sys.exit(2)
    with open(sys.argv[1], encoding="utf-8") as f:
        ok, skip = convert(f, sys.stdout, sys.stderr)
    print(f"# 转换完成: {ok} 条成功, {skip} 条跳过(见 stderr)", file=sys.stderr)
