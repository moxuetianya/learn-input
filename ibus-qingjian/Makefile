PREFIX  ?= /usr
BINDIR  ?= $(PREFIX)/bin
DATADIR ?= $(PREFIX)/share

CC      ?= cc
CFLAGS  ?= -O2 -Wall -Wextra -std=c11 -D_GNU_SOURCE
PKGS     = ibus-1.0 json-glib-1.0 glib-2.0

BIN      = ibus-engine-qingjian

all: $(BIN) qingjian.component

$(BIN): src/main.c src/engine.c src/ipc.c src/keymap.c src/engine.h src/ipc.h src/keymap.h
	$(CC) $(CFLAGS) $(shell pkg-config --cflags $(PKGS)) -o $@ src/main.c src/engine.c src/ipc.c src/keymap.c $(shell pkg-config --libs $(PKGS))

qingjian.component: data/qingjian.component.in
	sed -e 's|@bindir@|$(BINDIR)|g' data/qingjian.component.in > $@

install: all
	install -Dm755 $(BIN) $(DESTDIR)$(BINDIR)/$(BIN)
	install -Dm644 qingjian.component $(DESTDIR)$(DATADIR)/ibus/component/qingjian.xml

uninstall:
	rm -f $(DESTDIR)$(BINDIR)/$(BIN) $(DESTDIR)$(DATADIR)/ibus/component/qingjian.xml

clean:
	rm -f $(BIN) qingjian.component

.PHONY: all install uninstall clean
