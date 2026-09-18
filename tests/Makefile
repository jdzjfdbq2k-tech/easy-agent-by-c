SHELL := cmd.exe

CC := gcc
CPPFLAGS += -I../include -I../src/tools -I../vendor/cJSON
CFLAGS ?= -std=c11 -Wall -Wextra
ROOT := ..
BUILD := $(ROOT)/build/tests
COMMON := $(ROOT)/src/util/strbuf.c $(ROOT)/vendor/cJSON/cJSON.c
TOOLS := $(wildcard $(ROOT)/src/tools/*.c)
HEADERS := $(wildcard $(ROOT)/include/*.h $(ROOT)/src/tools/*.h $(ROOT)/vendor/cJSON/*.h)
TARGETS := $(BUILD)/test_tools.exe $(BUILD)/test_registry.exe

all: $(TARGETS)
	@cd $(ROOT) && build\tests\test_tools.exe
	@cd $(ROOT) && build\tests\test_registry.exe

$(BUILD):
	@if not exist "$(subst /,\,$@)" mkdir "$(subst /,\,$@)"

$(BUILD)/test_tools.exe: test_tools.c $(COMMON) $(TOOLS) $(ROOT)/src/core/msgs.c $(HEADERS) | $(BUILD)
	$(CC) $(CPPFLAGS) $(CFLAGS) $(filter %.c,$^) $(LDFLAGS) -o $@

$(BUILD)/test_registry.exe: test_registry.c $(COMMON) $(ROOT)/src/tools/registry.c $(HEADERS) | $(BUILD)
	$(CC) $(CPPFLAGS) $(CFLAGS) $(filter %.c,$^) $(LDFLAGS) -o $@

clean:
	@if exist "$(subst /,\,$(BUILD))" rmdir /s /q "$(subst /,\,$(BUILD))"

.PHONY: all clean
