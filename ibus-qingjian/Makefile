# ibus-engine-qingjian：构建 / 测试 / 安装。
#
#   make            构建引擎与组件描述
#   make test       协议冒烟：自动起停 qingjian server（默认样例词库，DICT=real 用完整词库）
#   make install    装到 DESTDIR（PREFIX 可覆盖；组件固定装 <DATADIR>/ibus/component/qingjian.xml）
#   make clean
#
# 可覆盖变量：
#   SERVER_BIN  qingjian-linux-server 路径（默认 ../qingjian/target/release/…）
#   QJ_DIR      qingjian 仓库根（资源与样例词库来源）
#   VERSION     组件版本（默认 git describe）

PREFIX  ?= /usr
BINDIR  ?= $(PREFIX)/bin
DATADIR ?= $(PREFIX)/share

CC      ?= cc
CFLAGS  ?= -O2 -Wall -Wextra -std=c11 -D_GNU_SOURCE
PKGS     = ibus-1.0 json-glib-1.0 glib-2.0

VERSION  = $(shell git describe --tags --always --dirty 2>/dev/null || echo 0.1.0)
BIN      = ibus-engine-qingjian

QJ_DIR      ?= $(abspath ../qingjian)
SERVER_BIN  ?= $(QJ_DIR)/target/release/qingjian-linux-server
DICT        ?= sample   # sample | real

all: $(BIN) qingjian.component

$(BIN): src/main.c src/engine.c src/ipc.c src/keymap.c src/engine.h src/ipc.h src/keymap.h
	$(CC) $(CFLAGS) -DQJ_VERSION='"$(VERSION)"' \
		$(shell pkg-config --cflags $(PKGS)) -o $@ \
		src/main.c src/engine.c src/ipc.c src/keymap.c \
		$(shell pkg-config --libs $(PKGS))

qingjian.component: data/qingjian.component.in src/*.c src/*.h
	sed -e 's|@bindir@|$(BINDIR)|g' -e 's|@version@|$(VERSION)|g' data/qingjian.component.in > $@

# 协议冒烟：独立 0700 socket 目录 + 临时 XDG，server 由本目标起停。
test: $(BIN)
	@test -x $(SERVER_BIN) || { echo "缺 $(SERVER_BIN)，先 make server 或覆盖 SERVER_BIN"; exit 1; }
	@D=$$(mktemp -d); chmod 700 $$D; mkdir -p $$D/run; chmod 700 $$D/run; \
	if [ "$(DICT)" = real ]; then \
		DICT_ENV="QINGJIAN_RESOURCES=$(QJ_DIR)"; \
	else \
		DICT_ENV="QINGJIAN_RESOURCES=$(QJ_DIR) QINGJIAN_DICT=$(QJ_DIR)/assets/sample/dict.tsv"; \
	fi; \
	env $$DICT_ENV QINGJIAN_SOCKET=$$D/run/qingjian.sock \
		XDG_DATA_HOME=$$D/data XDG_CONFIG_HOME=$$D/config XDG_STATE_HOME=$$D/state \
		$(SERVER_BIN) & SRV=$$!; sleep 2; \
	QINGJIAN_SOCKET=$$D/run/qingjian.sock timeout 30 \
		python3 tests/smoke_test.py; RC=$$?; \
	kill $$SRV 2>/dev/null; wait $$SRV 2>/dev/null || true; rm -rf $$D; exit $$RC

install: all
	install -Dm755 $(BIN) $(DESTDIR)$(BINDIR)/$(BIN)
	install -Dm644 qingjian.component $(DESTDIR)$(DATADIR)/ibus/component/qingjian.xml

uninstall:
	rm -f $(DESTDIR)$(BINDIR)/$(BIN) $(DESTDIR)$(DATADIR)/ibus/component/qingjian.xml

clean:
	rm -f $(BIN) qingjian.component

.PHONY: all install uninstall clean test
