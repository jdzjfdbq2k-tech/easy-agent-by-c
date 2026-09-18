# Windows 主程序：make（macOS/Linux 需要 mingw-w64 交叉编译器）。
# 宿主平台测试：make test，无需 Windows SDK。
MINGW ?= x86_64-w64-mingw32
CC := $(MINGW)-gcc
CPPFLAGS += -Iinclude
CFLAGS ?= -O2 -Wall -Wextra -std=gnu11
LDLIBS ?= -lwinhttp

SRC := $(wildcard src/*.c src/tools/*.c)
OBJ := $(patsubst %.c,build/%.o,$(SRC))
BIN := build/agent.exe

all: $(BIN)

$(BIN): $(OBJ)
	$(CC) $(LDFLAGS) $(OBJ) -o $@ $(LDLIBS)

build/%.o: %.c
	@mkdir -p $(dir $@)
	$(CC) $(CPPFLAGS) $(CFLAGS) -MMD -MP -c $< -o $@

-include $(OBJ:.o=.d)

test:
	$(MAKE) -C tests

clean:
	$(RM) $(OBJ) $(OBJ:.o=.d) $(BIN)
	$(MAKE) -C tests clean

.PHONY: all test clean
