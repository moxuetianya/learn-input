#!/usr/bin/env python3
"""模拟 ibus-engine-qingjian 的协议行为做冒烟测试。

用法：smoke_test.py [--socket /path/to/qingjian.sock]

按引擎实现的顺序走一遍握手 + 按键流，检查：
1. OpenSession → Update.linux_ui.version == 3
2. LinuxHello → 版本与会话号
3. Capabilities → KeyResult
4. 逐键 "nihao" → 帧（preedit/候选/commit 空格后）
5. 候选带译词（有释义表时）
"""
import argparse
import json
import os
import socket
import struct
import sys
import uuid


class Client:
    def __init__(self, path):
        self.sock = socket.socket(socket.AF_UNIX, socket.SOCK_STREAM)
        self.sock.connect(path)

    def call(self, message):
        body = json.dumps(message, ensure_ascii=False).encode()
        self.sock.sendall(struct.pack("<I", len(body)) + body)
        prefix = self._read_exact(4)
        (length,) = struct.unpack("<I", prefix)
        return json.loads(self._read_exact(length))

    def _read_exact(self, n):
        data = b""
        while len(data) < n:
            chunk = self.sock.recv(n - len(data))
            if not chunk:
                raise ConnectionError("server closed")
            data += chunk
        return data


def main():
    parser = argparse.ArgumentParser()
    parser.add_argument("--socket", default=os.environ.get("QINGJIAN_SOCKET") or
                        os.path.join(os.environ["XDG_RUNTIME_DIR"], "qingjian.sock"))
    args = parser.parse_args()

    client = Client(args.socket)
    session = 1
    generation = 1
    context = f"smoke-{uuid.uuid4().hex[:12]}"

    reply = client.call({"OpenSession": {"session": session, "app": "smoke", "protocol": 8}})
    update = reply.get("Update", {})
    assert update.get("linux_ui", {}).get("version") == 3, f"OpenSession: {reply}"
    print("ok OpenSession (linux_ui v3)")

    reply = client.call({"LinuxHello": {"version": 3, "session": session,
                                        "generation": generation, "context": context}})
    hello = reply.get("LinuxHello", {})
    assert hello.get("version") == 3 and hello.get("session") == session, f"LinuxHello: {reply}"
    print(f"ok LinuxHello (preedit={hello.get('preedit')})")

    def event(ev):
        reply = client.call({"LinuxEvent": {"session": session, "event": ev}})
        result = reply.get("KeyResult")
        assert result is not None, f"no KeyResult: {reply}"
        assert result["session"] == session
        return result

    result = event({"Capabilities": {"sensitive": False, "password": False, "disabled": False}})
    print(f"ok Capabilities (outcome={result['outcome']})")

    result = event({"Focus": {"focused": True}})
    print(f"ok Focus (outcome={result['outcome']})")

    def key(char):
        vk = ord(char.lower())
        return event({"Key": {"event": {
            "virtual_key": vk, "character": char,
            "modifiers": {"ctrl": False, "shift": False, "alt": False,
                          "win": False, "caps": False, "english_mode": False}},
            "release": False}})

    last = None
    for ch in "nihao":
        last = key(ch)
        frame = last["frame"]
        texts = [i["text"] for i in frame["candidates"]["items"]]
        preedit = "".join(s["text"] for s in frame["preedit"])
        print(f"key {ch}: preedit={preedit!r} first_candidates={texts[:3]} commit={last['commit']!r}")
        assert last["outcome"] == "Consumed"
        assert texts, "no candidates"

    # 有释义表时第一条候选应带英文译词
    items = last["frame"]["candidates"]["items"]
    glossed = [i for i in items if (i.get("translation") or {}).get("senses")]
    if glossed:
        word = glossed[0]["text"]
        sense = glossed[0]["translation"]["senses"][0]["text"]
        print(f"ok translation: {word!r} -> {sense!r}")
    else:
        print("note: no translation senses in frame (glossary missing?)")

    # 空格上屏首选
    last = key(" ")
    assert last["commit"], f"space did not commit: {last}"
    print(f"ok commit by space: {last['commit']!r}")
    assert last["frame"]["candidates"]["items"] == [], "candidates should clear after commit"

    # CloseSession 不产生回包（协议约定），发完直接关闭。
    body = json.dumps({"CloseSession": {"session": session}}, ensure_ascii=False).encode()
    client.sock.sendall(struct.pack("<I", len(body)) + body)
    client.sock.close()
    print("ALL OK")
    return 0


if __name__ == "__main__":
    sys.exit(main())
