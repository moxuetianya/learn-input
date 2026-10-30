# learn-input 统一构建入口：引擎、server、数据、冒烟测试、部署。
#
#   make engine          构建 ibus-engine-qingjian（C，秒级）
#   make server          构建 qingjian-linux-server（Rust release）
#   make data            检查产品数据是否就位（缺失时给出下载指引）
#   make smoke           本地协议冒烟：自动起停 server，跑 tests/smoke_test.py
#   make deploy          全量部署到 DEST_HOST（默认 peter@192.168.2.102）
#   make deploy-engine   只重编重装引擎（迭代 C 前端时的秒级路径）
#   make clean
#
# 路径可覆盖（worktree / 共享构建产物时有用）：
#   make smoke QJ_DIR=/path/to/qingjian SERVER_BIN=/path/to/qingjian-linux-server
#   make deploy DEST_HOST=peter@192.168.2.103

QJ_DIR      ?= $(CURDIR)/qingjian
SERVER_BIN  ?= $(QJ_DIR)/target/release/qingjian-linux-server
ENGINE_DIR  ?= $(CURDIR)/ibus-qingjian
DEST_HOST   ?= peter@192.168.2.102
SMOKE_DICT  ?= sample   # sample（快，协议验证）| real（完整词库）

.PHONY: engine server data smoke deploy deploy-engine clean help

engine:
	$(MAKE) -C $(ENGINE_DIR)

server:
	RUSTUP_TOOLCHAIN=stable cargo build --release --manifest-path $(QJ_DIR)/Cargo.toml -p qingjian-linux-server

data:
	@test -f $(QJ_DIR)/data/generated/dict.qj \
		&& echo "产品数据就位：$(QJ_DIR)/data/generated/dict.qj" \
		|| { echo "产品数据缺失。下载（需代理）："; \
		     echo "  cd $(QJ_DIR) && ./tools/release/data-fetch.sh"; exit 1; }

# 冒烟测试：临时 XDG 目录里起 server，跑完自动回收，socket 目录按 server 要求 0700。
smoke: engine
	$(MAKE) -C $(ENGINE_DIR) test SERVER_BIN=$(SERVER_BIN) QJ_DIR=$(QJ_DIR) DICT=$(SMOKE_DICT)

deploy: engine server data
	DEST_HOST=$(DEST_HOST) ./scripts/deploy-102.sh

deploy-engine: engine
	DEST_HOST=$(DEST_HOST) ./scripts/deploy-102.sh --engine-only

clean:
	$(MAKE) -C $(ENGINE_DIR) clean

help:
	@grep -E '^#   make ' Makefile | sed 's/^#   //'
