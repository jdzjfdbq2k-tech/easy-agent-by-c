# 平台无关测试；从项目根目录或 tests/ 中都可以启动。
CC ?= cc
CPPFLAGS += -I../include -I../src/tools
CFLAGS ?= -std=c11 -Wall -Wextra
ROOT := ..
BUILD := $(ROOT)/build/tests
COMMON := $(ROOT)/src/strbuf.c $(ROOT)/src/cJSON.c
TOOLS := $(wildcard $(ROOT)/src/tools/*.c)
HEADERS := $(wildcard $(ROOT)/include/*.h $(ROOT)/src/tools/*.h)
TARGETS := $(BUILD)/test_tools $(BUILD)/test_registry $(BUILD)/test_permissions

all: $(TARGETS)
	cd $(ROOT) && ./build/tests/test_tools
	cd $(ROOT) && ./build/tests/test_registry
	cd $(ROOT) && ./build/tests/test_permissions

$(BUILD):
	mkdir -p $@

$(BUILD)/test_tools: test_tools.c $(COMMON) $(TOOLS) $(ROOT)/src/msgs.c $(ROOT)/src/permissions.c $(HEADERS) | $(BUILD)
	$(CC) $(CPPFLAGS) $(CFLAGS) $(filter %.c,$^) $(LDFLAGS) -o $@

# 用两个测试工具替换 builtins.c，验证扩展不需要改 registry.c。
$(BUILD)/test_registry: test_registry.c $(COMMON) $(ROOT)/src/tools/registry.c $(HEADERS) | $(BUILD)
	$(CC) $(CPPFLAGS) $(CFLAGS) $(filter %.c,$^) $(LDFLAGS) -o $@

$(BUILD)/test_permissions: test_permissions.c $(ROOT)/src/permissions.c $(HEADERS) | $(BUILD)
	$(CC) $(CPPFLAGS) $(CFLAGS) $(filter %.c,$^) $(LDFLAGS) -o $@

clean:
	$(RM) $(TARGETS)

.PHONY: all clean
