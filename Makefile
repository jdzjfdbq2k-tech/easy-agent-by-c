# easy-agent-by-c
#
# 目标平台是 Windows，所以在 macOS / Linux 上用 mingw-w64 交叉编译：
#   macOS          brew install mingw-w64
#   Debian/Ubuntu  sudo apt install mingw-w64
#   Windows(MSYS2) pacman -S mingw-w64-x86_64-gcc
#
# 然后：
#   make          编译出 agent.exe
#   make clean    清掉中间产物
#
# 交叉编译出来的 exe 在 macOS 上跑不了（没有 Windows 运行时），
# 它只能替你回答一个很重要的问题：代码能不能编译通过、类型对不对。
# 真正跑起来还是要在 Windows 上。

# 用 := 而不是 ?=。
# 很多 shell 环境里已经导出了 CC=cc，而 ?= 会被环境变量顶掉，
# 结果就是用宿主编译器去编 windows.h，报一堆头文件找不到。
# 用 := 固定默认值；命令行上的 make CC=... 依然优先级最高，可以覆盖。
MINGW ?= x86_64-w64-mingw32
CC    := $(MINGW)-gcc

CFLAGS ?= -O2 -Wall -Wextra -std=gnu11 -Iinclude
LDLIBS ?= -lwinhttp

SRC := $(wildcard src/*.c)
OBJ := $(SRC:.c=.o)
BIN := agent.exe

all: $(BIN)

$(BIN): $(OBJ)
	$(CC) $(OBJ) -o $@ $(LDLIBS)

src/%.o: src/%.c
	$(CC) $(CFLAGS) -c $< -o $@

# 小项目不值得维护头文件依赖图，头一变就全量重编。
$(OBJ): $(wildcard include/*.h)

clean:
	rm -f $(OBJ) $(BIN)

.PHONY: all clean
