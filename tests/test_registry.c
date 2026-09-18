#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "tool_definition.h"
#include "tools.h"
#include "strbuf.h"

#define CHECK(condition) do { \
    if (!(condition)) { \
        fprintf(stderr, "FAIL at line %d: %s\n", __LINE__, #condition); \
        return 1; \
    } \
} while (0)

static int echo_calls;
static int second_calls;

static char *echo(const cJSON *args) {
    echo_calls++;
    const cJSON *text = cJSON_GetObjectItemCaseSensitive(args, "text");
    strbuf out;
    sb_init(&out);
    sb_puts(&out, cJSON_IsString(text) ? text->valuestring : "empty");
    return sb_detach(&out);
}

static char *second(const cJSON *args) {
    (void)args;
    second_calls++;
    return NULL; /* 注册表须将执行失败转成可回填给模型的错误文本。 */
}

/* 仅测试定义可变，用于注入错误注册；生产定义均为 const。 */
static tool_definition first_definition = {
    "echo", "quote: \" newline:\n", "{\"type\":\"object\"}", echo
};
static tool_definition second_definition = {
    "second", "second tool", "{\"type\":\"object\"}", second
};
const tool_definition *const builtin_tools[] = {
    &first_definition, &second_definition
};
const size_t builtin_tools_count = 2;

int main(void) {
    char *schema = tools_schema_json();
    CHECK(schema != NULL);
    cJSON *root = cJSON_Parse(schema);
    CHECK(cJSON_IsArray(root) && cJSON_GetArraySize(root) == 2);
    for (int i = 0; i < 2; i++) {
        cJSON *entry = cJSON_GetArrayItem(root, i);
        cJSON *type = cJSON_GetObjectItem(entry, "type");
        CHECK(cJSON_IsString(type) && strcmp(type->valuestring, "function") == 0);
        cJSON *fn = cJSON_GetObjectItem(entry, "function");
        cJSON *name = cJSON_GetObjectItem(fn, "name");
        cJSON *description = cJSON_GetObjectItem(fn, "description");
        CHECK(cJSON_IsString(name));
        CHECK(strcmp(name->valuestring, builtin_tools[i]->name) == 0);
        CHECK(cJSON_IsString(description));
        CHECK(strcmp(description->valuestring, builtin_tools[i]->description) == 0);
        CHECK(cJSON_IsObject(cJSON_GetObjectItem(fn, "parameters")));
        char *out = tool_run(name->valuestring, "{\"text\":\"hello\\nworld\"}");
        CHECK(out != NULL);
        CHECK(i == 0 ? strcmp(out, "hello\nworld") == 0 : strstr(out, "error:") == out);
        free(out);
    }
    CHECK(echo_calls == 1 && second_calls == 1);
    cJSON_Delete(root);
    free(schema);

    const char *invalid[] = {"[]", "null", "42", "\"text\"", "{} trailing", "{broken"};
    for (size_t i = 0; i < sizeof(invalid) / sizeof(invalid[0]); i++) {
        char *out = tool_run("echo", invalid[i]);
        CHECK(out && strstr(out, "error:") == out);
        free(out);
    }
    CHECK(echo_calls == 1); /* 非对象、残缺和尾随垃圾都不应进入执行器。 */
    char *out = tool_run("missing", "{}");
    CHECK(out && strstr(out, "unknown tool"));
    free(out);
    CHECK(second_calls == 1);

    out = tool_run("echo", "");
    CHECK(out && strcmp(out, "empty") == 0);
    free(out);
    out = tool_run("echo", NULL);
    CHECK(out && strcmp(out, "empty") == 0);
    free(out);

    second_definition.name = "echo";
    CHECK(tools_schema_json() == NULL);
    out = tool_run("echo", "{}");
    CHECK(out && strstr(out, "invalid tool registry"));
    free(out);
    second_definition.name = "second";
    second_definition.execute = NULL;
    CHECK(tools_schema_json() == NULL);
    second_definition.execute = second;

    const char *bad_schema[] = {"[]", "{}", "{\"type\":\"string\"}", "{broken", "{} trailing"};
    for (size_t i = 0; i < sizeof(bad_schema) / sizeof(bad_schema[0]); i++) {
        second_definition.parameters_json = bad_schema[i];
        CHECK(tools_schema_json() == NULL);
    }
    second_definition.parameters_json = "{\"type\":\"object\"}";
    schema = tools_schema_json();
    CHECK(schema != NULL);
    free(schema);
    puts("REGISTRY ALL PASS (multiple tools, dispatch, validation, error paths)");
    return 0;
}
