SHELL := cmd.exe

CC := gcc
CPPFLAGS += -Iinclude -Ivendor/cJSON
CFLAGS ?= -O2 -Wall -Wextra -std=gnu11
LDLIBS ?= -lwinhttp

SRC := $(wildcard src/*.c src/*/*.c) vendor/cJSON/cJSON.c
OBJ := $(patsubst %.c,build/%.o,$(SRC))
BIN := build/agent.exe

OBJDIR = $(subst /,\,$(@D))

all: $(BIN)

$(BIN): $(OBJ)
	$(CC) $(LDFLAGS) $(OBJ) -o $@ $(LDLIBS)

build/%.o: %.c
	@if not exist "$(OBJDIR)" mkdir "$(OBJDIR)"
	$(CC) $(CPPFLAGS) $(CFLAGS) -MMD -MP -c $< -o $@

-include $(OBJ:.o=.d)

test:
	$(MAKE) -C tests

clean:
	@if exist build rmdir /s /q build

.PHONY: all test clean
