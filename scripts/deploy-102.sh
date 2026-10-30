#!/usr/bin/env bash
# 部署 qingjian IBus 输入法到目标机（默认 peter@192.168.2.102）。
#
#   scripts/deploy-102.sh                 # 全量：构建 → 同步 → 安装 → 注册 → 激活验证
#   scripts/deploy-102.sh --engine-only   # 只重编重装引擎（迭代 C 前端时的秒级路径）
#   scripts/deploy-102.sh --sync-only     # 只构建与同步文件，不改目标机系统配置
#
# 目标机布局：
#   ~/.local/qingjian/bin/qingjian-linux-server     Rust 引擎服务（systemd --user）
#   ~/.local/qingjian/share/qingjian/resources/     词库/模型/释义表（server 的 QINGJIAN_RESOURCES 布局）
#   /usr/local/bin/ibus-engine-qingjian             IBus 前端（C）
#   /usr/share/ibus/component/qingjian.xml          IBus 组件注册（registry 只认 .xml 后缀）
set -euo pipefail

ROOT="$(cd "$(dirname "$0")/.." && pwd)"
QJ="${QJ_DIR:-$ROOT/qingjian}"
IBUS_DIR="${ROOT}/ibus-qingjian"
DEST_HOST="${DEST_HOST:-peter@192.168.2.102}"
RES_DIR=".local/qingjian/share/qingjian/resources"
MODE=all
case "${1:-}" in
  --engine-only) MODE=engine ;;
  --sync-only) MODE=sync ;;
esac

log() { printf '\033[1;32m==>\033[0m %s\n' "$*"; }

# ---------- 1. 本机构建（101 与 102 同为 Ubuntu 24.04 / glibc 2.39） ----------
if [[ $MODE == engine ]]; then
  log "构建 ibus-engine-qingjian（engine-only：跳过 server 与数据）"
  ( cd "$IBUS_DIR" && make )
else
  log "构建 qingjian-linux-server（release，增量）"
  ( cd "$QJ" && RUSTUP_TOOLCHAIN=stable cargo build --release -p qingjian-linux-server )
  log "构建 ibus-engine-qingjian"
  ( cd "$IBUS_DIR" && make )
fi

SERVER_BIN="$QJ/target/release/qingjian-linux-server"
ENGINE_BIN="$IBUS_DIR/ibus-engine-qingjian"
COMPONENT="$IBUS_DIR/qingjian.component"
VERSION="$("$ENGINE_BIN" --version | awk '{print $2}')"
[[ -x "$ENGINE_BIN" && -f "$COMPONENT" ]] || { echo "构建产物缺失" >&2; exit 1; }
[[ $MODE == engine ]] || [[ -x "$SERVER_BIN" ]] || { echo "缺 $SERVER_BIN" >&2; exit 1; }
log "版本：$VERSION"

# ---------- 2. 同步到目标机 ----------
ssh_cmd() { GPG_TTY=/dev/nonexistent ssh -o BatchMode=yes "$DEST_HOST" "$@"; }

log "同步二进制到 ${DEST_HOST}"
ssh_cmd "mkdir -p ~/.local/qingjian/bin ~/.cache/qingjian-deploy"
rsync -a --info=stats1 "$ENGINE_BIN" "$COMPONENT" "$DEST_HOST:.cache/qingjian-deploy/"

if [[ $MODE != engine ]]; then
  log "同步资源到 ${DEST_HOST}:${RES_DIR}（rsync 增量）"
  ssh_cmd "mkdir -p ${RES_DIR}/data"
  rsync -a --info=stats1 "$SERVER_BIN" "$DEST_HOST:.local/qingjian/bin/qingjian-linux-server"
  rsync -a --delete --info=stats1 \
      "$QJ/data/generated/" "$DEST_HOST:${RES_DIR}/data/generated/"
  rsync -a --delete --info=stats1 \
      "$QJ/data/models/" "$DEST_HOST:${RES_DIR}/data/models/"
  rsync -a --delete --info=stats1 \
      "$QJ/assets/" "$DEST_HOST:${RES_DIR}/assets/"
fi

[[ $MODE == sync ]] && { log "--sync-only：完成"; exit 0; }

# ---------- 3. 目标机安装 ----------
if [[ $MODE == engine ]]; then
  log "重装引擎二进制（engine-only）"
  ssh_cmd "sudo -n install -m755 ~/.cache/qingjian-deploy/ibus-engine-qingjian /usr/local/bin/ibus-engine-qingjian"
else
  log "安装 IBus 前端与组件描述（sudo）"
  ssh_cmd "sudo -n install -Dm755 ~/.cache/qingjian-deploy/ibus-engine-qingjian /usr/local/bin/ibus-engine-qingjian \
      && sudo -n install -Dm644 ~/.cache/qingjian-deploy/qingjian.component /usr/share/ibus/component/qingjian.xml"

  log "安装并启动 systemd 用户服务"
  ssh_cmd "mkdir -p ~/.config/systemd/user && cat > ~/.config/systemd/user/qingjian.service <<'UNIT'
[Unit]
Description=Qingjian Linux IME server (dictionary + sentence model + LLM predict)

[Service]
ExecStart=%h/.local/qingjian/bin/qingjian-linux-server
Restart=on-failure
RestartSec=2

[Install]
WantedBy=default.target
UNIT
systemctl --user daemon-reload && systemctl --user enable --now qingjian"

  log "注册输入源并重启 IBus"
  ssh_cmd 'export DBUS_SESSION_BUS_ADDRESS="${DBUS_SESSION_BUS_ADDRESS:-unix:path=/run/user/$(id -u)/bus}"
current=$(gsettings get org.gnome.desktop.input-sources sources 2>/dev/null || echo "[]")
case "$current" in
  *qingjian*) echo "输入源已注册" ;;
  *) gsettings set org.gnome.desktop.input-sources sources "${current%]}, (\"ibus\", \"qingjian\")]" ;;
esac
# GNOME 的 ibus-daemon 由 systemd 用户单元管理；registry 缓存要清掉才会重扫组件目录
pkill -f "^/usr/local/bin/ibus-engine" 2>/dev/null || true
rm -rf ~/.cache/ibus/bus/registry
systemctl --user restart org.freedesktop.IBus.session.GNOME 2>/dev/null || ibus restart'
fi

# ---------- 4. 激活与验证 ----------
log "激活引擎并验证链路"
ssh_cmd 'export DBUS_SESSION_BUS_ADDRESS="${DBUS_SESSION_BUS_ADDRESS:-unix:path=/run/user/$(id -u)/bus}"
pkill -f "^/usr/local/bin/ibus-engine" 2>/dev/null || true; sleep 0.5
ibus engine qingjian 2>/dev/null || ibus engine qingjian   # setxkbmap 警告无碍，重试取结果
echo "当前引擎：$(ibus engine)"
sleep 2
pgrep -f "^/usr/local/bin/ibus-engine" >/dev/null && echo "engine-ok $(/usr/local/bin/ibus-engine-qingjian --version)" || { echo "engine 进程未起" >&2; exit 1; }
if systemctl --user is-active --quiet qingjian 2>/dev/null; then echo "server-ok"; else echo "server 未运行" >&2; exit 1; fi
ss -x 2>/dev/null | grep -q qingjian.sock && echo "link-ok（引擎↔server ESTABLISHED）" || echo "warn: 暂无 ESTABLISHED（桌面无聚焦窗口时正常，点开任意输入框后建立）"'

log "完成（$VERSION）。桌面打字即可验证；云联想见 ~/.config/qingjian/config.toml 的 [predict]。"
