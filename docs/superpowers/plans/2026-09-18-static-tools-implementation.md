# Static Built-in Tools Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Refactor the current single `read_file` tool into a static registry so tool metadata, JSON Schema, and handlers share one source of truth while preserving the current public behavior.

**Architecture:** Keep the minimal two-file shape: `include/tools.h` defines the cJSON-backed tool types and existing execution API; `src/tools.c` owns the static registry, schema generation, lookup, argument parsing, and the `read_file` handler. Do not add plugin loading, tool contexts, sandboxing, or a new JSON abstraction.

**Tech Stack:** C11/gnu11, cJSON 1.7.19 already vendored in the repository, MinGW for the Windows executable, and the existing platform-independent C test harness.

**Spec:** `docs/superpowers/specs/2026-09-18-static-tools-design.md`

## Global Constraints

- Keep the simplest static built-in tool design; do not implement dynamic DLLs, separate tool processes, or a plugin framework.
- Continue using the repository's existing cJSON implementation.
- Keep `read_file` in `src/tools.c` for this change; split files only after the project has roughly 3–4 tools.
- Tool failures return strings beginning with `error:` instead of `NULL`.
- `tool_run()` owns parsing and deleting the cJSON arguments object; tool handlers must not retain or delete it.
- Tool output is heap-owned by the caller and must be released with `free()`.
- Do not modify the existing `.gitignore` worktree change.

---

### Task 1: Add the public registry contract test

**Files:**
- Modify: `tests/test_tools.c` near the existing schema tests and `main()` test list
- Read: `include/tools.h` to keep the test aligned with the public API

**Interfaces:**
- Consumes: the planned `cJSON`-backed `tool_handler` and `tool_definition` declarations from `include/tools.h`.
- Produces: a compile-time/runtime test that locks the minimal registry entry shape before implementation.

- [ ] **Step 1: Add a probe handler and contract test**

Add this near the other test helpers:

```c
static char *probe_handler(const cJSON *args) {
    (void)args;
    return NULL;
}

static void test_tool_definition_contract(void) {
    printf("== tool_definition contract ==\n");

    tool_definition def = {
        "probe",
        "probe tool",
        "{}",
        probe_handler
    };

    CHECK(strcmp(def.name, "probe") == 0, "definition stores the tool name");
    CHECK(strcmp(def.description, "probe tool") == 0,
          "definition stores the description");
    CHECK(strcmp(def.parameters_json, "{}") == 0,
          "definition stores parameter schema text");
    CHECK(def.handler == probe_handler, "definition stores the handler");
}
```

Call `test_tool_definition_contract()` from `main()` after `test_schema()`.

- [ ] **Step 2: Run the focused test build and verify it fails before implementation**

Run from the repository root:

```powershell
$testExe = Join-Path $env:TEMP 'easy-agent-test-tools.exe'
cc -std=c11 -Wall -Wextra -Iinclude tests/test_tools.c src/strbuf.c src/tools.c src/msgs.c src/cJSON.c -o $testExe
```

Expected: compilation fails because `tool_definition` is not yet declared in the current `include/tools.h`.

- [ ] **Step 3: Commit the failing contract test**

```bash
git add tests/test_tools.c
git commit -m "test: define static tool registry contract"
```

### Task 2: Implement the static registry and generated Schema

**Files:**
- Modify: `include/tools.h`
- Modify: `src/tools.c`

**Interfaces:**
- Consumes: the contract test from Task 1.
- Produces: `tool_handler`, `tool_definition`, `tools_schema_json()`, and `tool_run()` behavior used by `llm.c`, `main.c`, and the tests.

- [ ] **Step 1: Add the cJSON-backed public types to `include/tools.h`**

Add the cJSON include and declarations:

```c
#include "cJSON.h"

typedef char *(*tool_handler)(const cJSON *args);

typedef struct {
    const char *name;
    const char *description;
    const char *parameters_json;
    tool_handler handler;
} tool_definition;
```

Keep the existing `tool_call`, `tool_call_free()`, `tools_schema_json()`, and `tool_run()` declarations unchanged unless the compiler requires ordering adjustments.

- [ ] **Step 2: Replace the standalone Schema string with one registry entry**

In `src/tools.c`, define a parameter object string for `read_file` and a registry array:

```c
#define READ_FILE_PARAMETERS_JSON \
    "{\"type\":\"object\",\"properties\":{" \
    "\"path\":{\"type\":\"string\",\"description\":\"文件路径，相对于工作目录，例如 src/main.c\"}}," \
    "\"required\":[\"path\"]}"
static const tool_definition tools[] = {
    {
        "read_file",
        "读取一个文本文件的内容并返回。用于查看源码。",
        READ_FILE_PARAMETERS_JSON,
        tool_read_file
    }
};

#define TOOL_COUNT (sizeof(tools) / sizeof(tools[0]))
```

Preserve the current model-facing description (读取一个文本文件的内容并返回。用于查看源码。); the important invariant is that the name, description, parameter JSON, and handler live in this one entry.

- [ ] **Step 3: Change `tool_read_file()` to the registry handler signature**

Keep the current file-reading, 4000-byte cap, UTF-8 boundary handling, empty-file marker, and error text. Only change its entry point to:

```c
static char *tool_read_file(const cJSON *args);
```

Do not move the implementation to a new file in this change.

- [ ] **Step 4: Implement lookup and generic dispatch**

Add a private lookup helper:

```c
static const tool_definition *find_tool(const char *name) {
    for (size_t i = 0; i < TOOL_COUNT; i++) {
        if (strcmp(tools[i].name, name) == 0) return &tools[i];
The handler must never receive ownership of args; tool_run() deletes it after the handler returns.
    return NULL;
}
```

Update `tool_run()` to preserve all current error behavior while dispatching through the registry:

```c
char *tool_run(const char *name, const char *arguments_json) {
    strbuf out;
    sb_init(&out);

    if (!name) {
        sb_puts(&out, "error: tool name is null");
        return sb_detach(&out);
    }

    const char *text = (arguments_json && *arguments_json)
        ? arguments_json : "{}";
    cJSON *args = cJSON_Parse(text);
    if (!args) {
        sb_printf(&out, "error: arguments is not valid JSON: %s", text);
        return sb_detach(&out);
    }

    const tool_definition *def = find_tool(name);
    if (!def) {
        sb_printf(&out, "error: unknown tool \"%s\"", name);
        cJSON_Delete(args);
        return sb_detach(&out);
    }

    char *result = def->handler(args);
    cJSON_Delete(args);

    if (!result) {
        sb_puts(&out, "error: tool handler failed");
        return sb_detach(&out);
    }

    sb_free(&out);
    return result;
}
```

The handler must never receive ownership of args; tool_run() deletes it after the handler returns.

- [ ] **Step 5: Generate the Schema from the same registry**

Replace the hard-coded `SCHEMA_JSON` return with a one-time cached cJSON array. For each registry entry:

```c
cJSON *entry = cJSON_CreateObject();
cJSON_AddStringToObject(entry, "type", "function");

cJSON *function = cJSON_AddObjectToObject(entry, "function");
cJSON_AddStringToObject(function, "name", tools[i].name);
cJSON_AddStringToObject(function, "description", tools[i].description);

cJSON *parameters = cJSON_Parse(tools[i].parameters_json);
cJSON_AddItemToObject(function, "parameters", parameters);
cJSON_AddItemToArray(root, entry);
```

Print the root with `cJSON_PrintUnformatted()`, cache the returned pointer for the process lifetime, delete the temporary cJSON tree, and return the cached `const char *`. If any allocation or parameter parse fails, return `NULL` and do not cache an invalid value.

- [ ] **Step 6: Run the focused tests and verify they pass**

```powershell
$testExe = Join-Path $env:TEMP 'easy-agent-test-tools.exe'
cc -std=c11 -Wall -Wextra -Iinclude tests/test_tools.c src/strbuf.c src/tools.c src/msgs.c src/cJSON.c -o $testExe
& $testExe
```

Expected: `ALL PASS`, including the existing `read_file`, truncation, UTF-8, error, and message serialization tests.

- [ ] **Step 7: Commit the registry implementation**

```bash
git add include/tools.h src/tools.c tests/test_tools.c
git commit -m "refactor: dispatch built-in tools through registry"
```

### Task 3: Verify the Windows build and integration contract

**Files:**
- Read: `Makefile`
- Read: `src/llm.c`
- Read: `src/main.c`
- Modify: none expected

**Interfaces:**
- Consumes: the registry-backed `tools_schema_json()` and `tool_run()` from Task 2.
- Produces: evidence that the LLM request path and Windows executable still compile without source changes outside the tool layer.

- [ ] **Step 1: Run the full platform-independent test suite**

Use the repository's normal command when `make` is available:

```bash
make -C tests
```

In the current environment, where `make` is unavailable, use the direct command from Task 2 and require `ALL PASS`.

- [ ] **Step 2: Compile the complete Windows target to a temporary output**

```powershell
$agentExe = Join-Path $env:TEMP 'easy-agent-build-check.exe'
x86_64-w64-mingw32-gcc -O2 -Wall -Wextra -std=gnu11 -Iinclude src/*.c -lwinhttp -o $agentExe
```

Expected: exit code 0. The existing MinGW warning about ignoring `#pragma comment(lib, "winhttp.lib")` is acceptable because the command explicitly links `-lwinhttp`.

- [ ] **Step 3: Remove only temporary verification executables**

```powershell
Remove-Item -LiteralPath (Join-Path $env:TEMP 'easy-agent-test-tools.exe'), (Join-Path $env:TEMP 'easy-agent-build-check.exe') -Force -ErrorAction SilentlyContinue
```

- [ ] **Step 4: Check the worktree and commit only intended files**

```bash
git status --short
git diff --check
```

Expected: the pre-existing `.gitignore` modification remains untouched; no generated test executable or temporary fixture is tracked.

## Self-Review Checklist

- The static registry is the single source for tool name, description, parameter Schema, and handler.
- `tool_run()` still returns non-`NULL` error strings for null names, unknown tools, malformed JSON, and handler failures.
- `read_file` behavior and output truncation remain unchanged.
- cJSON arguments are parsed once and deleted by `tool_run()` after the handler returns.
- The plan does not introduce dynamic loading, sandboxing, or unrelated refactoring.
- Every spec requirement has a corresponding task: minimal structure (Task 2), direct cJSON use (Task 2), static registration (Task 2), error conventions (Task 2), and tests (Tasks 1 and 3).
