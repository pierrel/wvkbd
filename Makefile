include config.mk

.PHONY: all clean format install test test-cli test-sanitize

NAME=wvkbd
BIN?=${NAME}-${LAYOUT}
SRC=.
MAN1 = ${NAME}.1

PKGS = wayland-client xkbcommon pangocairo

WVKBD_SOURCES += $(wildcard $(SRC)/*.c)
WVKBD_HEADERS += $(wildcard $(SRC)/*.h)

PKG_CONFIG ?= pkg-config
CFLAGS += -std=gnu99 -Wall -g -DWITH_WAYLAND_SHM -DLAYOUT=\"layout.${LAYOUT}.h\" -DKEYMAP=\"keymap.${LAYOUT}.h\"
CFLAGS += $(shell $(PKG_CONFIG) --cflags $(PKGS))
LDFLAGS += $(shell $(PKG_CONFIG) --libs $(PKGS)) -lm -lutil -lrt

WAYLAND_HEADERS = $(wildcard proto/*.xml)

HDRS = $(WAYLAND_HEADERS:.xml=-client-protocol.h)
WAYLAND_SRC = $(HDRS:.h=.c)
SOURCES = $(WVKBD_SOURCES) $(WAYLAND_SRC)

SCDOC=scdoc
DOCS = wvkbd.1
TEST_BIN = tests/test-mod-swipe tests/test-glide tests/bench-glide

OBJECTS = $(SOURCES:.c=.o)

all: ${BIN} ${DOCS}

config.h:
	cp config.def.h config.h

proto/%-client-protocol.c: proto/%.xml
	wayland-scanner code < $? > $@

proto/%-client-protocol.h: proto/%.xml
	wayland-scanner client-header < $? > $@

$(OBJECTS): $(HDRS) $(WVKBD_HEADERS)

${BIN}: config.h $(OBJECTS) layout.${LAYOUT}.h
	$(CC) -o $@ $(OBJECTS) $(LDFLAGS)

clean:
	rm -f $(OBJECTS) $(HDRS) $(WAYLAND_SRC) ${BIN} ${DOCS} ${TEST_BIN}

test: ${TEST_BIN} test-cli
	./tests/test-mod-swipe
	./tests/test-glide
	./tests/bench-glide --max-us 50000

tests/test-mod-swipe: tests/test-mod-swipe.c mod-swipe.c mod-swipe.h letters.c letters.h
	$(CC) -std=c99 -Wall -Wextra -Werror -I. -o $@ tests/test-mod-swipe.c mod-swipe.c letters.c

tests/test-glide: tests/test-glide.c glide.c glide.h glide-words-en.h
	$(CC) -std=c11 -Wall -Wextra -Werror -I. -o $@ tests/test-glide.c glide.c

tests/bench-glide: tests/bench-glide.c glide.c glide.h glide-words-en.h
	$(CC) -std=c11 -Wall -Wextra -Werror -I. -o $@ tests/bench-glide.c glide.c

test-sanitize:
	$(MAKE) clean
	$(MAKE) CFLAGS='-std=gnu99 -Wall -g -fsanitize=address,undefined' LDFLAGS='-fsanitize=address,undefined' tests/test-mod-swipe tests/test-glide
	ASAN_OPTIONS=detect_leaks=1 ./tests/test-mod-swipe
	ASAN_OPTIONS=detect_leaks=1 ./tests/test-glide

test-cli: ${BIN}
	tests/test-cli.sh ./${BIN}

format:
	clang-format -i $(WVKBD_SOURCES) $(WVKBD_HEADERS)

%: %.scd
	$(SCDOC) < $< > $@

install: all
	mkdir -p ${DESTDIR}${PREFIX}/bin
	cp -f ${BIN} ${DESTDIR}${PREFIX}/bin
	chmod 755 ${DESTDIR}${PREFIX}/bin/${BIN}
	mkdir -p "${DESTDIR}${MANPREFIX}/man1"
	sed "s/VERSION/${VERSION}/g" < ${MAN1} > ${DESTDIR}${MANPREFIX}/man1/${MAN1}
	chmod 644 ${DESTDIR}${MANPREFIX}/man1/${MAN1}
