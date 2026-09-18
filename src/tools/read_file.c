#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "tool_definition.h"
#include "strbuf.h"
#include "file_io.h"

#define TOOL_OUTPUT_MAX 4000

/* read_file 的实现                                                    */

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

    FILE *fp = file_open(path->valuestring, L"rb");
    if (!fp) {
        sb_printf(&out, "error: cannot open file: \"%s\"",
                  path->valuestring);
        return sb_detach(&out);
    }

    char buf[1024];
    size_t n;
    int truncated = 0;

    while ((n = fread(buf, 1, sizeof(buf), fp)) > 0) {
        if (out.len + n >= TOOL_OUTPUT_MAX) {

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

const tool_definition tool_read_file_definition = {
    .name = "read_file",
    .description = "读取一个文本文件的内容并返回。用于查看源码。",
    .parameters_json =
        "{\"type\":\"object\",\"properties\":{"
        "\"path\":{\"type\":\"string\","
        "\"description\":\"文件路径，相对路径以当前工作目录为基准，例如 src/main.c\"}},"
        "\"required\":[\"path\"]}",
    .execute = tool_read_file
};
