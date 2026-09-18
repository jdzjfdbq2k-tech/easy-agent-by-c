#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <windows.h>

#include "cJSON.h"
#include "client.h"
#include "llm.h"
#include "tools.h"

/* ------------------------------------------------------------------ */
/* 小工具                                                              */
/* ------------------------------------------------------------------ */

static wchar_t *to_wide(const char *s) {
    if (!s) return NULL;

    int n = MultiByteToWideChar(CP_UTF8, 0, s, -1, NULL, 0);
    if (!n) return NULL;

    wchar_t *w = malloc(sizeof(wchar_t) * n);
    if (!w) return NULL;

    if (!MultiByteToWideChar(CP_UTF8, 0, s, -1, w, n)) {
        free(w);
        return NULL;
    }

    return w;
}

static wchar_t *make_auth(const char *key) {
    size_t n = strlen(key) + 32;
    char *buf = malloc(n);
    if (!buf) return NULL;

    snprintf(buf, n, "Authorization: Bearer %s\r\n", key);

    wchar_t *w = to_wide(buf);
    free(buf);

    return w;
}

/* 复制 cJSON 里的字符串到普通堆内存。
 *
 * 为什么要复制：cJSON_Delete(root) 会连同所有 valuestring 一起释放，
 * 而我们希望在解析完就丢掉整棵树，只留下需要的那几个字段。
 * 用 malloc 而不是 cJSON_malloc，是为了和调用方的 free 配对 ——
 * "谁 malloc 谁 free" 的约定一旦被打破，后面就会有人用错释放函数。 */
static char *dup_str(const char *s) {
    if (!s) return NULL;

    size_t n = strlen(s) + 1;
    char *p = malloc(n);
    if (p) memcpy(p, s, n);

    return p;
}

/* ------------------------------------------------------------------ */
/* 组装请求                                                            */
/* ------------------------------------------------------------------ */

static char *make_json(const char *model, const char *messages_json) {
    cJSON *root = cJSON_CreateObject();
    if (!root) return NULL;

    cJSON_AddStringToObject(root, "model", model);

    /* messages 现在是**一整段数组文本**，parse 一次挂上去就行。
     * 本模块不认识消息的内部结构（谁是 user、谁是 tool），那是 msgs.c 的事。
     * 和下面 tools 的处理方式完全一致。 */
    cJSON *messages = cJSON_Parse(messages_json);
    if (!messages) {
        fprintf(stderr, "[llm] messages_json is not valid JSON:\n%s\n",
                messages_json ? messages_json : "(null)");
        cJSON_Delete(root);
        return NULL;
    }
    cJSON_AddItemToObject(root, "messages", messages);

    /* 工具 schema 和执行分发都来自同一份注册表。 */
    char *schema = tools_schema_json();
    cJSON *tools = schema ? cJSON_Parse(schema) : NULL;
    free(schema);
    if (!tools) {
        cJSON_Delete(root);
        return NULL;
    }
    cJSON_AddItemToObject(root, "tools", tools);

    char *json = cJSON_PrintUnformatted(root);
    cJSON_Delete(root);

    return json;
}

/* ------------------------------------------------------------------ */
/* 解析响应                                                            */
/* ------------------------------------------------------------------ */

static llm_result *parse_response(const char *response) {
    cJSON *root = cJSON_Parse(response);
    if (!root) {
        fprintf(stderr, "[llm] response is not valid JSON:\n%s\n", response);
        return NULL;
    }

    /* 接口出错时会返回 {"error": {...}}。原来的代码在这里直接返回 NULL，
     * 结果只打印一句 "LLM call failed"，你完全不知道是 key 错了、
     * 模型名错了、还是上下文超了。把原始响应打出来，调试成本直接降到零。 */
    if (cJSON_GetObjectItem(root, "error")) {
        fprintf(stderr, "[llm] API returned an error:\n%s\n", response);
        cJSON_Delete(root);
        return NULL;
    }

    const cJSON *choices = cJSON_GetObjectItem(root, "choices");
    const cJSON *choice = cJSON_GetArrayItem(choices, 0);
    const cJSON *message = cJSON_GetObjectItem(choice, "message");

    if (!message) {
        fprintf(stderr, "[llm] no choices[0].message in response:\n%s\n", response);
        cJSON_Delete(root);
        return NULL;
    }

    llm_result *r = calloc(1, sizeof(*r));
    if (!r) {
        cJSON_Delete(root);
        return NULL;
    }

    /* 分支一：普通回答 */
    const cJSON *content = cJSON_GetObjectItem(message, "content");
    if (cJSON_IsString(content)) {
        r->content = dup_str(content->valuestring);
    }

    /* 分支二：工具调用请求 */
    const cJSON *calls = cJSON_GetObjectItem(message, "tool_calls");
    int n = cJSON_GetArraySize(calls);

    if (n > 0) {
        r->calls = calloc((size_t)n, sizeof(tool_call));
        if (r->calls) {
            /* 这里直接把 call_count 设成 n，而不是"成功解析出几个算几个"。
             * 因为 calls 是 calloc 出来的，字段全是 NULL，
             * 即使某一项解析失败，释放路径也是安全的。
             * 反过来如果只记成功的数量，未计入的那些项就永远泄漏了。 */
            r->call_count = n;

            for (int i = 0; i < n; i++) {
                const cJSON *c = cJSON_GetArrayItem(calls, i);
                const cJSON *id = cJSON_GetObjectItem(c, "id");
                const cJSON *fn = cJSON_GetObjectItem(c, "function");
                const cJSON *name = cJSON_GetObjectItem(fn, "name");
                const cJSON *args = cJSON_GetObjectItem(fn, "arguments");

                if (cJSON_IsString(id))   r->calls[i].id = dup_str(id->valuestring);
                if (cJSON_IsString(name)) r->calls[i].name = dup_str(name->valuestring);
                /* arguments 拿出来是一段**字符串**，原样存着，
                 * 交给 tools.c 去 parse。在这里不解析，是因为 llm 层
                 * 不该知道每个工具的参数长什么样。 */
                if (cJSON_IsString(args)) r->calls[i].arguments = dup_str(args->valuestring);
            }
        }
    }

    /* 把助手这条消息**原样**序列化下来，供调用方回填进历史。
     *
     * 两件事必须注意：
     *   1. 位置：一定要在 cJSON_Delete(root) 之前。放在后面就是野指针 ——
     *      这是这段代码里最容易写错的一行。
     *   2. 方式：不要自己拼一条 {"role":"assistant",...}。模型还可能返回
     *      reasoning_content 之类的字段，各家服务商要求保留哪些、丢掉哪些
     *      并不一致。原样照抄保真度最高。
     *
     * 额外做一次 dup_str 是为了让 llm_result 的四个字段全部由 malloc 持有，
     * 释放时统一用 free，不用去记哪个字段该用 cJSON_free。 */
    char *printed = cJSON_PrintUnformatted(message);
    if (printed) {
        r->assistant_json = dup_str(printed);
        cJSON_free(printed);
    }

    cJSON_Delete(root);
    return r;
}

/* ------------------------------------------------------------------ */
/* 对外接口                                                            */
/* ------------------------------------------------------------------ */

llm_result *call_llm(const char *messages_json) {
    const char *host_s = getenv("LLM_HOST");
    const char *path_s = getenv("LLM_PATH");
    const char *model = getenv("LLM_MODEL");
    const char *key = getenv("LLM_API_KEY");

    if (!host_s || !path_s || !model || !key) {
        fprintf(stderr, "[llm] missing env: need LLM_HOST / LLM_PATH / LLM_MODEL / LLM_API_KEY\n");
        return NULL;
    }

    wchar_t *host = to_wide(host_s);
    wchar_t *path = to_wide(path_s);
    wchar_t *auth = make_auth(key);
    char *json = make_json(model, messages_json);

    if (!host || !path || !auth || !json) {
        free(host);
        free(path);
        free(auth);
        cJSON_free(json);
        fprintf(stderr, "[llm] failed to build request\n");
        return NULL;
    }

    char *response = client_post_json(host, path, json, auth);

    free(host);
    free(path);
    free(auth);
    cJSON_free(json);

    if (!response) {
        fprintf(stderr, "[llm] http request failed (check network / LLM_HOST)\n");
        return NULL;
    }

    llm_result *r = parse_response(response);
    free(response);

    return r;
}

void llm_result_free(llm_result *r) {
    if (!r) return;

    free(r->content);
    free(r->assistant_json);

    for (int i = 0; i < r->call_count; i++) {
        tool_call_free(&r->calls[i]);
    }
    free(r->calls);
    free(r);
}
