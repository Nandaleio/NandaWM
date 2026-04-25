CC      = cc
WLR_PROTOCOLS     = /usr/share/wayland-protocols
WLR_PROTOCOLS_WLR = /home/nandaleio/Workspace/wlroots/protocol
SCANNER = wayland-scanner

CFLAGS  = -std=c11 -Wall -Wextra -Wno-unused-parameter -Isrc -Ibuild \
          -DWLR_USE_UNSTABLE \
          $(shell pkg-config --cflags wlroots-0.18 wayland-server xkbcommon)
LDFLAGS = $(shell pkg-config --libs   wlroots-0.18 wayland-server xkbcommon)

SRCS = src/main.c src/output.c src/input.c src/view.c src/layer.c
OBJS = $(SRCS:.c=.o)

PROTO_HDRS = build/xdg-shell-protocol.h \
             build/wlr-layer-shell-unstable-v1-protocol.h

all: $(PROTO_HDRS) nandawm

build/xdg-shell-protocol.h: $(WLR_PROTOCOLS)/stable/xdg-shell/xdg-shell.xml
	@mkdir -p build
	$(SCANNER) client-header $< $@

build/wlr-layer-shell-unstable-v1-protocol.h: $(WLR_PROTOCOLS_WLR)/wlr-layer-shell-unstable-v1.xml
	@mkdir -p build
	$(SCANNER) server-header $< $@

nandawm: $(OBJS)
	$(CC) -o $@ $^ $(LDFLAGS)

%.o: %.c src/nanda.h $(PROTO_HDRS)
	$(CC) $(CFLAGS) -c -o $@ $<

clean:
	rm -f $(OBJS) nandawm
	rm -rf build

.PHONY: all clean
