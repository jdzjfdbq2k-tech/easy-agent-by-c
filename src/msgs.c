#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "cJSON.h"
#include "msgs.h"
#include "strbuf.h"

#define MSGS_INIT_CAP 4

typedef struct msgs {
    size_t count;      /* 当前消息数量 */
    size_t capacity;   /* 已分配的指针槽数量 */
    char **messages;   /* 每条消息是一段 JSON 文本 */
} msgs;

/* ------------------------------------------------------------------ */
/* 内部：扩容 + 追加                                                   */
/* ------------------------------------------------------------------ */

/*
 * 把一条已经构造好的 JSON 文本收进数组，接管它的所有权。
 *
 * 这个函数存在的意义不只是少写代码。原来三个 add 各有一份扩容逻辑，
 * 意味着"realloc 失败怎么办"要写对三遍。现在只有这一处，改对了就是全对了。
 *
 * 关键是扩容的顺序：先 realloc 到临时变量，**成功之后**才写回
 * m->messages 和 m->capacity。反过来写（先改 capacity 再 realloc）的话，
 * 一旦失败就同时踩三个坑：
 *   1. 原来那块内存的唯一指针丢了 —— 泄漏
 *   2. capacity 说有 8 个位置，缓冲却是 NULL —— 结构体自相矛盾
 *   3. 下一行往 NULL[0] 写 —— 段错误
 */
static int msgs_push(msgs *m, char *owned) {
    if (!owned) return 0;

    if (m->count >= m->capacity) {
        size_t cap = m->capacity ? m->capacity * 2 : MSGS_INIT_CAP;

        char **grown = realloc(m->messages, cap * sizeof(char *));
        if (!grown) {
            cJSON_free(owned);   /* 收不下就把这条消息也放掉，不留悬空内存 */
            return 0;
        }

        m->messages = grown;
        m->capacity = cap;
    }

    m->messages[m->count++] = owned;
    return 1;
}

/* ------------------------------------------------------------------ */
/* 生命周期                                                            */
/* ------------------------------------------------------------------ */

msgs *msgs_new(void) {
    msgs *m = malloc(sizeof(msgs));
    if (!m) return NULL;

    m->count = 0;
    m->capacity = MSGS_INIT_CAP;
    m->messages = malloc(m->capacity * sizeof(char *));

    if (!m->messages) {
        free(m);
        return NULL;
    }

    return m;
}

void msgs_free(msgs *m) {
    if (!m) return;

    /* 先用 cJSON_free 逐条放掉内容，再放指针数组，最后放结构体。
     * 顺序不能变：先放 m 就再也找不到那些字符串了。 */
    for (size_t i = 0; i < m->count; i++) {
        cJSON_free(m->messages[i]);
    }

    free(m->messages);
    free(m);
}

/* ------------------------------------------------------------------ */
/* 追加消息                                                            */
/* ------------------------------------------------------------------ */

void msgs_add_user(msgs *m, const char *text) {
    if (!m || !text) return;

    cJSON *obj = cJSON_CreateObject();
    if (!obj) return;

    cJSON_AddStringToObject(obj, "role", "user");
    cJSON_AddStringToObject(obj, "content", text);   /* 转义交给 cJSON */

    char *msg_json = cJSON_PrintUnformatted(obj);
    cJSON_Delete(obj);

    msgs_push(m, msg_json);
}

void msgs_add_assistant_raw(msgs *m, const char *message_json) {
    if (!m || !message_json) return;

    /* 这里 parse 一遍再 print 回去，看着像脱裤子放屁，其实白捡一个校验：
     * 模型返回的如果不是合法 JSON，整条就被丢掉，坏数据进不到下一轮请求体里。 */
    cJSON *obj = cJSON_Parse(message_json);
    if (!obj) return;

    char *msg_json = cJSON_PrintUnformatted(obj);
    cJSON_Delete(obj);

    msgs_push(m, msg_json);
}

void msgs_add_tool_result(msgs *m, const char *tool_call_id, const char *output) {
    if (!m || !tool_call_id || !output) return;

    cJSON *obj = cJSON_CreateObject();
    if (!obj) return;

    cJSON_AddStringToObject(obj, "role", "tool");
    cJSON_AddStringToObject(obj, "tool_call_id", tool_call_id);
    /* output 是文件内容，里面全是双引号、反斜杠、换行。
     * 手工拼字符串这里必炸，交给 cJSON 转义。 */
    cJSON_AddStringToObject(obj, "content", output);

    char *msg_json = cJSON_PrintUnformatted(obj);
    cJSON_Delete(obj);

    msgs_push(m, msg_json);
}

/* ------------------------------------------------------------------ */
/* 序列化                                                              */
/* ------------------------------------------------------------------ */

/*
 * 把整个历史拼成一个 JSON 数组文本。每条消息本身已经是一段合法 JSON，
 * 所以这里只需要在中间插逗号、两头加方括号 —— 不需要再做任何转义。
 *
 * 用 strbuf 而不是 snprintf 到固定缓冲区：历史会一轮一轮变长，
 * 长度不可预知，固定缓冲区迟早溢出。
 */
char *msgs_to_json(const msgs *m) {
    strbuf sb;
    sb_init(&sb);

    sb_putc(&sb, '[');

    if (m) {
        for (size_t i = 0; i < m->count; i++) {
            if (i > 0) sb_putc(&sb, ',');
            sb_puts(&sb, m->messages[i]);
        }
    }

    sb_putc(&sb, ']');

    /* 空历史走到这里自然得到 "[]"，符合 msgs.h 里的约定，不需要特判 */
    return sb_detach(&sb);
}

int msgs_count(const msgs *m) {
    return m ? (int)m->count : 0;
}
