#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "cJSON.h"
#include "strbuf.h"
#include "tools.h"

/* 单个工具的输出上限。超了就截断。
 *
 * 为什么现在就要做：模型很容易兴奋地让你读一个大文件。几万字符塞进对话，
 * 下一次请求就会超出上下文长度，接口直接报错。等出了问题再回头加，
 * 你会花很多时间才定位到原因。 */
#define TOOL_OUTPUT_MAX 4000

/* ------------------------------------------------------------------ */
/* 工具定义：给模型看的说明书                                          */
/* ------------------------------------------------------------------ */

static const char *SCHEMA_JSON =
"["
"  {"
"    \"type\": \"function\","
"    \"function\": {"
"      \"name\": \"read_file\","
"      \"description\": \"读取一个文本文件的内容并返回。用于查看源码。\","
"      \"parameters\": {"
"        \"type\": \"object\","
"        \"properties\": {"
"          \"path\": {"
"            \"type\": \"string\","
"            \"description\": \"文件路径，相对于工作目录，例如 src/main.c\""
"          }"
"        },"
"        \"required\": [\"path\"]"
"      }"
"    }"
"  }"
"]";

const char *tools_schema_json(void) {
    return SCHEMA_JSON;
}

/* ------------------------------------------------------------------ */
/* read_file 的实现                                                    */
/* ------------------------------------------------------------------ */

/*
 * 把切点从"可能落在多字节字符中间"的位置，退回到最近的 UTF-8 字符边界。
 *
 * 为什么必须做这件事：
 *   4000 是个字节数，而中文字符占 3 个字节（emoji 占 4 个）。如果第 4000
 *   个字节正好是某个中文字符的第 2 个字节，那切出来的就是一个残缺的
 *   字节序列 —— 合法 JSON 里不允许出现非法 UTF-8，严格的服务端会直接
 *   拒绝整个请求，而错误信息一般只说"请求格式错误"，根本不会指向这里。
 *
 * 判据：UTF-8 的后续字节（continuation byte）形如 10xxxxxx。
 * 如果切点那个字节是后续字节，说明它属于前面那个字符，必须继续往前退。
 * 一个 UTF-8 字符最多 4 字节，所以最多退 3 次。
 */
static size_t utf8_boundary(const char *s, size_t n) {
    while (n > 0 && ((unsigned char)s[n] & 0xC0) == 0x80) {
        n--;
    }
    return n;
}

static char *tool_read_file(const cJSON *args) {
    strbuf out;
    sb_init(&out);

    /* arguments 已经被 tool_run 解析成对象了，这里再按名字取字段。
     * 一定要校验类型：模型有概率把 path 写成数字或嵌套对象。 */
    const cJSON *path = cJSON_GetObjectItemCaseSensitive(args, "path");
    if (!cJSON_IsString(path)) {
        sb_puts(&out, "error: missing required string parameter \"path\"");
        return sb_detach(&out);
    }

    FILE *fp = fopen(path->valuestring, "rb");
    if (!fp) {
        sb_printf(&out, "error: cannot open file \"%s\"", path->valuestring);
        return sb_detach(&out);
    }

    char buf[1024];
    size_t n;
    int truncated = 0;

    while ((n = fread(buf, 1, sizeof(buf), fp)) > 0) {
        if (out.len + n >= TOOL_OUTPUT_MAX) {
            /* 多收 4 个字节再停。
             * 因为下面要判断"切点那个字节是不是多字节字符的一半"，
             * 而那个判断需要看到切点本身 —— 只收到 4000 就停的话，
             * 恰好跨界时根本看不到下一个字节。 */
            size_t keep_end = TOOL_OUTPUT_MAX + 4;
            size_t want = (out.len >= keep_end) ? 0 : keep_end - out.len;
            if (want > n) want = n;
            sb_write(&out, buf, want);
            truncated = 1;
            break;
        }
        sb_write(&out, buf, n);
    }

    fclose(fp);

    if (truncated) {
        /* 先退到 UTF-8 字符边界，再补上截断说明。
         * 注意这里必须自己改 out.len，不能只截字符串。 */
        size_t cut = (out.len > TOOL_OUTPUT_MAX) ? TOOL_OUTPUT_MAX : out.len;
        cut = utf8_boundary(out.data, cut);
        out.len = cut;
        out.data[cut] = '\0';

        sb_printf(&out, "\n... [truncated, file is longer than %d bytes]",
                  TOOL_OUTPUT_MAX);
    }

    if (out.len == 0) {
        sb_puts(&out, "(empty file)");
    }

    return sb_detach(&out);
}

/* ------------------------------------------------------------------ */
/* 分发                                                                */
/* ------------------------------------------------------------------ */

char *tool_run(const char *name, const char *arguments_json) {
    strbuf out;
    sb_init(&out);

    if (!name) {
        sb_puts(&out, "error: tool name is null");
        return sb_detach(&out);
    }

    /* 模型偶尔会给出空字符串或残缺 JSON。统一兜底成 {}，
     * 让下面的参数校验去报"缺参数"，而不是在这里说"JSON 坏了" ——
     * 后者对模型来说信息量更小。 */
    const char *text = (arguments_json && *arguments_json) ? arguments_json : "{}";

    cJSON *args = cJSON_Parse(text);
    if (!args) {
        sb_printf(&out, "error: arguments is not valid JSON: %s", text);
        return sb_detach(&out);
    }

    char *result = NULL;

    if (strcmp(name, "read_file") == 0) {
        result = tool_read_file(args);
    } else {
        sb_printf(&out, "error: unknown tool \"%s\"", name);
        result = sb_detach(&out);
    }

    cJSON_Delete(args);
    return result;
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
