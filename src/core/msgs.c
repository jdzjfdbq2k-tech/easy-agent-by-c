#include <stdlib.h>
#include <string.h>

#include "cJSON.h"
#include "msgs.h"

struct msgs {
    cJSON *items;
};

msgs *msgs_new(void) {
    msgs *m = malloc(sizeof(*m));
    if (!m) return NULL;
    m->items = cJSON_CreateArray();
    if (!m->items) { free(m); return NULL; }
    return m;
}

void msgs_free(msgs *m) {
    if (!m) return;
    cJSON_Delete(m->items);
    free(m);
}

/* 数组持有追加消息的所有权。 */
static void append(msgs *m, cJSON *message) {
    if (!message) return;
    if (!m || !cJSON_AddItemToArray(m->items, message)) cJSON_Delete(message);
}

static void add_text(msgs *m, const char *role, const char *text) {
    if (!m || !text) return;
    cJSON *message = cJSON_CreateObject();
    if (!message) return;
    if (!cJSON_AddStringToObject(message, "role", role) ||
        !cJSON_AddStringToObject(message, "content", text)) {
        cJSON_Delete(message);
        return;
    }
    append(m, message);
}

void msgs_add_system(msgs *m, const char *text) {
    add_text(m, "system", text);
}

void msgs_add_user(msgs *m, const char *text) {
    add_text(m, "user", text);
}

void msgs_add_assistant_raw(msgs *m, const char *message_json) {
    if (!m || !message_json) return;
    /* 保留 tool_calls、reasoning_content 等模型返回字段。 */
    append(m, cJSON_Parse(message_json));
}

void msgs_add_tool_result(msgs *m, const char *tool_call_id, const char *output) {
    if (!m || !tool_call_id || !output) return;
    cJSON *message = cJSON_CreateObject();
    if (!message) return;
    if (!cJSON_AddStringToObject(message, "role", "tool") ||
        !cJSON_AddStringToObject(message, "tool_call_id", tool_call_id) ||
        !cJSON_AddStringToObject(message, "content", output)) {
        cJSON_Delete(message);
        return;
    }
    append(m, message);
}

char *msgs_to_json(const msgs *m) {
    char *printed = m ? cJSON_PrintUnformatted(m->items) : NULL;
    if (m && !printed) return NULL;
    const char *text = printed ? printed : "[]";
    size_t size = strlen(text) + 1;
    /* 公共接口返回值统一用 free 释放。 */
    char *result = malloc(size);
    if (result) memcpy(result, text, size);
    cJSON_free(printed);
    return result;
}

int msgs_count(const msgs *m) {
    return m ? cJSON_GetArraySize(m->items) : 0;
}
