# JDC-AX6600 Athena LED Controller (pure C edition)
#
# Standard targets:  make / make clean / make install / make test
# Standard variables respected: CC, CFLAGS, LDFLAGS, DESTDIR, PREFIX
#
# The project intentionally avoids CMake, autotools, Rust and any third-party
# library. It only requires a C99/C11 compiler and POSIX/Linux interfaces, so
# an OpenWrt SDK toolchain can build it directly (CC=aarch64-openwrt-linux-gcc).

CC       ?= cc
CFLAGS   ?= -O2
LDFLAGS  ?=
LDLIBS   += -lm
PREFIX   ?= /usr
BINDIR   ?= $(PREFIX)/bin
DESTDIR  ?=

BIN      := athena-led-c
SRCDIR   := src
INCDIR   := include
BUILDDIR := build

# -std=c11 keeps the code strictly standard C; POSIX APIs (clock_gettime,
# termios, /proc, /sys) are opt-in via _POSIX_C_SOURCE.
CPPFLAGS += -I$(INCDIR)
CFLAGS   += -std=c11 -Wall -Wextra -Wpedantic -Wshadow -Wconversion \
            -D_POSIX_C_SOURCE=200809L

SRCS := $(sort $(wildcard $(SRCDIR)/*.c))
OBJS := $(patsubst $(SRCDIR)/%.c,$(BUILDDIR)/%.o,$(SRCS))

.PHONY: all clean install test

all: $(BIN)

$(BIN): $(OBJS)
	$(CC) $(LDFLAGS) -o $@ $(OBJS) $(LDLIBS)

$(BUILDDIR)/%.o: $(SRCDIR)/%.c | $(BUILDDIR)
	$(CC) $(CPPFLAGS) $(CFLAGS) -c $< -o $@

$(BUILDDIR):
	mkdir -p $@

install: $(BIN)
	install -d $(DESTDIR)$(BINDIR)
	install -m 0755 $(BIN) $(DESTDIR)$(BINDIR)/$(BIN)

clean:
	rm -f $(OBJS) $(BIN)
	rm -rf $(BUILDDIR)

test: $(BIN)
	$(CURDIR)/$(BIN) --self-test
