include config.mk

.PHONY: all clean format fuzz install test test-cli test-dictionary test-sanitize

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
TEST_BIN = tests/test-mod-swipe tests/test-glide tests/test-keyboard-glide tests/test-glide-geometry tests/test-main-glide-release tests/bench-glide
TEST_CFLAGS ?= -std=c11 -Wall -Wextra -Werror -I.
TEST_LDFLAGS ?=

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
	rm -f $(OBJECTS) $(HDRS) $(WAYLAND_SRC) ${BIN} ${DOCS} ${TEST_BIN} tests/fuzz-glide

test: config.h ${TEST_BIN} test-cli
	./tests/test-mod-swipe
	./tests/test-glide
	./tests/test-keyboard-glide
	./tests/test-glide-geometry
	./tests/test-main-glide-release
	./tests/bench-glide

tests/test-mod-swipe: config.h tests/test-mod-swipe.c mod-swipe.c mod-swipe.h glide.h letters.c letters.h
	$(CC) $(TEST_CFLAGS) -o $@ tests/test-mod-swipe.c mod-swipe.c letters.c $(TEST_LDFLAGS)

tests/test-glide: config.h tests/test-glide.c glide.c glide.h glide-words-en.h
	$(CC) $(TEST_CFLAGS) -o $@ tests/test-glide.c glide.c $(TEST_LDFLAGS)

tests/test-keyboard-glide: config.h tests/test-keyboard-glide.c keyboard.c keyboard.h drw.h glide.h letters.c letters.h os-compatibility.h layout.${LAYOUT}.h keymap.${LAYOUT}.h $(HDRS)
	$(CC) $(TEST_CFLAGS) -ffunction-sections -fdata-sections \
		-DLAYOUT=\"layout.$(LAYOUT).h\" -DKEYMAP=\"keymap.$(LAYOUT).h\" \
		-D_XOPEN_SOURCE=700 -DVERSION=\"$(VERSION)\" \
		-Wno-unused-parameter -Wno-missing-field-initializers -Wno-sign-compare \
		$(shell $(PKG_CONFIG) --cflags $(PKGS)) -o $@ \
		tests/test-keyboard-glide.c keyboard.c letters.c \
		-Wl,--gc-sections -Wl,--wrap=wl_proxy_get_version \
		-Wl,--wrap=wl_proxy_marshal_flags $(TEST_LDFLAGS)

tests/test-glide-geometry: config.h tests/test-glide-geometry.c keyboard.c keyboard.h drw.h glide.c glide.h glide-words-en.h mod-swipe.c mod-swipe.h letters.c letters.h os-compatibility.h layout.${LAYOUT}.h keymap.${LAYOUT}.h $(HDRS)
	$(CC) $(TEST_CFLAGS) -ffunction-sections -fdata-sections \
		-DLAYOUT=\"layout.$(LAYOUT).h\" -DKEYMAP=\"keymap.$(LAYOUT).h\" \
		-D_XOPEN_SOURCE=700 -DVERSION=\"$(VERSION)\" \
		-Wno-unused-parameter -Wno-missing-field-initializers -Wno-sign-compare \
		$(shell $(PKG_CONFIG) --cflags $(PKGS)) -o $@ \
		tests/test-glide-geometry.c keyboard.c glide.c mod-swipe.c letters.c \
		-Wl,--gc-sections -Wl,--wrap=wl_proxy_get_version \
		-Wl,--wrap=wl_proxy_marshal_flags $(TEST_LDFLAGS)

tests/test-main-glide-release: config.h tests/test-main-glide-release.c main.c keyboard.h drw.h mod-swipe.c mod-swipe.h glide.c glide.h glide-words-en.h letters.c letters.h os-compatibility.h layout.${LAYOUT}.h keymap.${LAYOUT}.h $(HDRS) $(WAYLAND_SRC)
	$(CC) $(TEST_CFLAGS) -ffunction-sections -fdata-sections \
		-DLAYOUT=\"layout.$(LAYOUT).h\" -DKEYMAP=\"keymap.$(LAYOUT).h\" \
		-D_XOPEN_SOURCE=700 -DVERSION=\"$(VERSION)\" \
		-Wno-unused-parameter -Wno-missing-field-initializers -Wno-sign-compare \
		$(shell $(PKG_CONFIG) --cflags $(PKGS)) -o $@ \
		tests/test-main-glide-release.c mod-swipe.c glide.c letters.c $(WAYLAND_SRC) \
		-Wl,--gc-sections -Wl,--wrap=wl_proxy_get_version \
		-Wl,--wrap=wl_proxy_marshal_flags -Wl,--wrap=wl_proxy_destroy \
		-Wl,--wrap=wl_proxy_add_listener \
		$(LDFLAGS) $(TEST_LDFLAGS)

tests/bench-glide: config.h tests/bench-glide.c glide.c glide.h glide-words-en.h
	$(CC) $(TEST_CFLAGS) -o $@ tests/bench-glide.c glide.c $(TEST_LDFLAGS)

test-dictionary:
	tests/test-glide-dictionary.sh "$(GLIDE_DICTIONARY_SOURCE)"

test-sanitize:
	$(MAKE) clean
	$(MAKE) TEST_CFLAGS='-std=c11 -Wall -Wextra -Werror -I. -g -fsanitize=address,undefined' TEST_LDFLAGS='-fsanitize=address,undefined' tests/test-mod-swipe tests/test-glide tests/test-glide-geometry
	ASAN_OPTIONS=detect_leaks=1 ./tests/test-mod-swipe
	ASAN_OPTIONS=detect_leaks=1 ./tests/test-glide
	ASAN_OPTIONS=detect_leaks=1 ./tests/test-glide-geometry

tests/fuzz-glide: config.h tests/fuzz-glide.c glide.c glide.h glide-words-en.h
	clang -std=c11 -Wall -Wextra -Werror -I. -g -fsanitize=fuzzer,address,undefined -o $@ tests/fuzz-glide.c glide.c

fuzz: config.h tests/fuzz-glide
	ASAN_OPTIONS=detect_leaks=0 timeout --preserve-status 75 ./tests/fuzz-glide -max_total_time=60 -max_len=65 -timeout=2

test-cli: config.h tests/test-cli.sh ${BIN}
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
