#!/usr/bin/env bash
# 部署 qingjian IBus 输入法到目标机（默认 peter@192.168.2.102）。
#
#   scripts/deploy-102.sh              # 全量：构建 → 同步 → 安装 → 注册 → 验证
#   scripts/deploy-102.sh --sync-only  # 只构建与同步文件，不改目标机系统配置
#
# 目标机布局：
#   ~/.local/qingjian/bin/qingjian-linux-server     Rust 引擎服务（systemd --user）
#   ~/.local/qingjian/share/qingjian/resources/     词库/模型/释义表（server 的 QINGJIAN_RESOURCES 布局）
#   /usr/local/bin/ibus-engine-qingjian             IBus 前端（C）
#   /usr/share/ibus/component/qingjian.component    IBus 组件注册
set -euo pipefail

ROOT="$(cd "$(dirname "$0")/.." && pwd)"
QJ="${ROOT}/qingjian"
IBUS_DIR="${ROOT}/ibus-qingjian"
DEST_HOST="${QJ_DEST:-peter@192.168.2.102}"
RES_DIR=".local/qingjian/share/qingjian/resources"
SYNC_ONLY=0
[[ "${1:-}" == "--sync-only" ]] && SYNC_ONLY=1

log() { printf '\033[1;32m==>\033[0m %s\n' "$*"; }

# ---------- 1. 本机构建（101 与 102 同为 Ubuntu 24.04 / glibc 2.39） ----------
log "构建 qingjian-linux-server（release）"
( cd "$QJ" && RUSTUP_TOOLCHAIN=stable cargo build --release -p qingjian-linux-server )

log "构建 ibus-engine-qingjian"
( cd "$IBUS_DIR" && make clean >/dev/null && make BINDIR=/usr/local/bin )

SERVER_BIN="$QJ/target/release/qingjian-linux-server"
ENGINE_BIN="$IBUS_DIR/ibus-engine-qingjian"
COMPONENT="$IBUS_DIR/qingjian.component"
[[ -x "$SERVER_BIN" && -x "$ENGINE_BIN" && -f "$COMPONENT" ]] || { echo "构建产物缺失" >&2; exit 1; }

# ---------- 2. 同步到目标机 ----------
ssh_cmd() { GPG_TTY=/dev/nonexistent ssh -o BatchMode=yes "$DEST_HOST" "$@"; }

log "同步二进制与资源到 ${DEST_HOST}"
ssh_cmd "mkdir -p ~/.local/qingjian/bin ${RES_DIR}/data ~/.cache/qingjian-deploy"
rsync -a --info=stats1 "$SERVER_BIN" "$DEST_HOST:.local/qingjian/bin/qingjian-linux-server"
rsync -a --delete --info=stats1 \
    "$QJ/data/generated/" "$DEST_HOST:${RES_DIR}/data/generated/"
rsync -a --delete --info=stats1 \
    "$QJ/data/models/" "$DEST_HOST:${RES_DIR}/data/models/"
rsync -a --delete --info=stats1 \
    "$QJ/assets/" "$DEST_HOST:${RES_DIR}/assets/"
rsync -a "$ENGINE_BIN" "$COMPONENT" "$DEST_HOST:.cache/qingjian-deploy/"

[[ $SYNC_ONLY -eq 1 ]] && { log "--sync-only：完成"; exit 0; }

# ---------- 3. 目标机安装 ----------
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

# ---------- 4. 验证 ----------
log "验证：组件在册 / 服务在跑 / socket 在听"
ssh_cmd "ibus list-engine | grep -i qingjian"
ssh_cmd "systemctl --user is-active qingjian"
ssh_cmd "test -S \${XDG_RUNTIME_DIR:-/run/user/\$(id -u)}/qingjian.sock && echo socket-ok"

log "完成。切换输入法（Super+Space）选「Qingjian」即可使用。"
log "云联想：~/.config/qingjian/config.toml 的 [predict] 填 api_key（或设 QINGJIAN_API_KEY），enabled=true，然后 systemctl --user restart qingjian。"
