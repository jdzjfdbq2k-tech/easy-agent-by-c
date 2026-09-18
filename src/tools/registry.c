#include <stdlib.h>
#include <string.h>

#include "tool_definition.h"
#include "tools.h"
#include "strbuf.h"

/* 拒绝残缺定义和重复名称，避免模型列表与执行分发发生歧义。 */
static int registry_valid(void) {
    for (size_t i = 0; i < builtin_tools_count; i++) {
        const tool_definition *t = builtin_tools[i];
        if (!t || !t->name || !*t->name || !t->description ||
            !t->parameters_json || !t->execute) return 0;
        for (size_t j = 0; j < i; j++) {
            if (strcmp(t->name, builtin_tools[j]->name) == 0) return 0;
        }
    }
    return 1;
}

char *tools_schema_json(void) {
    if (!registry_valid()) return NULL;
    cJSON *array = cJSON_CreateArray();
    if (!array) return NULL;

    for (size_t i = 0; i < builtin_tools_count; i++) {
        const tool_definition *t = builtin_tools[i];
        cJSON *entry = cJSON_CreateObject();
        if (!entry) goto fail;
        if (!cJSON_AddItemToArray(array, entry)) {
            cJSON_Delete(entry);
            goto fail;
        }
        if (!cJSON_AddStringToObject(entry, "type", "function")) goto fail;
        cJSON *fn = cJSON_AddObjectToObject(entry, "function");
        if (!fn || !cJSON_AddStringToObject(fn, "name", t->name) ||
            !cJSON_AddStringToObject(fn, "description", t->description)) goto fail;

        cJSON *params = cJSON_ParseWithOpts(t->parameters_json, NULL, 1);
        const cJSON *type = cJSON_GetObjectItemCaseSensitive(params, "type");
        if (!cJSON_IsObject(params) || !cJSON_IsString(type) ||
            strcmp(type->valuestring, "object") != 0) {
            cJSON_Delete(params);
            goto fail;
        }
        if (!cJSON_AddItemToObject(fn, "parameters", params)) {
            cJSON_Delete(params);
            goto fail;
        }
    }

    char *printed = cJSON_PrintUnformatted(array);
    cJSON_Delete(array);
    if (!printed) return NULL;
    /* 对外统一 malloc/free 所有权，不依赖 cJSON 的分配器配置。 */
    size_t len = strlen(printed) + 1;
    char *result = malloc(len);
    if (result) memcpy(result, printed, len);
    cJSON_free(printed);
    return result;

fail:
    cJSON_Delete(array);
    return NULL;
}

static char *tool_error(const char *message) {
    strbuf out;
    sb_init(&out);
    if (!sb_printf(&out, "error: %s", message)) {
        sb_free(&out);
        return NULL;
    }
    return sb_detach(&out);
}

char *tool_run(const char *name, const char *arguments_json) {
    if (!name) return tool_error("tool name is null");
    if (!registry_valid()) return tool_error("invalid tool registry");

    const tool_definition *tool = NULL;
    for (size_t i = 0; i < builtin_tools_count; i++) {
        if (strcmp(name, builtin_tools[i]->name) == 0) {
            tool = builtin_tools[i];
            break;
        }
    }
    if (!tool) return tool_error("unknown tool");

    const char *text = arguments_json && *arguments_json ? arguments_json : "{}";
    cJSON *args = cJSON_ParseWithOpts(text, NULL, 1);
    if (!args) return tool_error("arguments is not valid JSON");
    if (!cJSON_IsObject(args)) {
        cJSON_Delete(args);
        return tool_error("arguments must be a JSON object");
    }

    char *result = tool->execute(args);
    cJSON_Delete(args);
    return result ? result : tool_error("tool execution failed (out of memory)");
}

void tool_call_free(tool_call *call) {
    if (!call) return;
    free(call->id);
    free(call->name);
    free(call->arguments);
    call->id = NULL;
    call->name = NULL;
    call->arguments = NULL;
}
