# 平台无关的单元测试。用宿主编译器（macOS/Linux 上的 cc）构建，
# 和生成 agent.exe 的那套 mingw 工具链完全无关。
#
#   make -C tests        构建并运行
#   make -C tests clean
#
# 必须在 tests/ 下用 -C 调用，因为用例里用了 src/main.c 这类相对路径。

CC      ?= cc
CFLAGS  ?= -std=c11 -Wall -Wextra -I../include

ROOT  := ..
TARGET := test_tools

# 平台无关的源文件清单。
# 条件：不 include windows.h / 不调用 WinHTTP / 不使用 _putenv_s。
# 以后每写一个这样的模块，就加到这里 —— 加进来的那一刻它就有了测试能力。

LIB_SRC := $(ROOT)/src/strbuf.c \
           $(ROOT)/src/tools.c \
           $(ROOT)/src/msgs.c \
           $(ROOT)/src/cJSON.c

all: $(TARGET)
	cd $(ROOT) && ./tests/$(TARGET)

$(TARGET): test_tools.c $(LIB_SRC)
	$(CC) $(CFLAGS) $^ -o $@

clean:
	rm -f $(TARGET) $(ROOT)/tmp_big.txt $(ROOT)/tmp_empty.txt

.PHONY: all clean
