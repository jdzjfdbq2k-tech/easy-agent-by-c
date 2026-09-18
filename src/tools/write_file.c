#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "tool_definition.h"
#include "file_io.h"
#include "strbuf.h"

/* write_file 的实现：局部编辑，不是整文件重写                          */

static size_t count_occurrences(const char *data, size_t len,
                                const char *old, size_t old_len,
                                size_t *first) {
    size_t count = 0;
    for (size_t i = 0; i + old_len <= len; i++) {
        if (memcmp(data + i, old, old_len) == 0) {
            if (count == 0) *first = i;
            count++;
        }
    }
    return count;
}

/* 把整个文件读进 strbuf。失败（读错误或内存不足）返回 0。 */
static int read_all(FILE *fp, strbuf *out) {
    char buf[4096];
    size_t n;
    while ((n = fread(buf, 1, sizeof(buf), fp)) > 0) {
        if (!sb_write(out, buf, n)) return 0;
    }
    return !ferror(fp);
}

static char *write_file(const cJSON *args) {
    strbuf out;
    sb_init(&out);

    const cJSON *path = cJSON_GetObjectItemCaseSensitive(args, "path");
    const cJSON *old_item = cJSON_GetObjectItemCaseSensitive(args, "old_string");
    const cJSON *new_item = cJSON_GetObjectItemCaseSensitive(args, "new_string");

    if (!cJSON_IsString(path) || !cJSON_IsString(old_item) || !cJSON_IsString(new_item)) {
        sb_puts(&out, "error: required string parameters \"path\", \"old_string\" and \"new_string\"");
        return sb_detach(&out);
    }

    const char *old_s = old_item->valuestring;
    const char *new_s = new_item->valuestring;
    size_t old_len = strlen(old_s);
    size_t new_len = strlen(new_s);

    /* 先尝试读旧内容。读不到 + old_string 非空 => 只能是"文件不存在"
     * 或"被权限拒绝"，两种情况模型都没有可编辑的原文，直接报错。 */
    FILE *fp = file_open(path->valuestring, L"rb");
    if (!fp) {
        if (old_len > 0) {
            sb_printf(&out, "error: file does not exist or cannot be read: \"%s\"",
                      path->valuestring);
            return sb_detach(&out);
        }

        /* 空旧串 + 文件不存在 = 新建。 */
        FILE *wf = file_open(path->valuestring, L"wbx");
        if (!wf) {
            sb_puts(&out, "error: cannot create file (must not exist; parent directory must exist)");
            return sb_detach(&out);
        }
        int ok = new_len == 0 || fwrite(new_s, 1, new_len, wf) == new_len;
        if (fclose(wf) != 0) ok = 0;
        if (ok) sb_printf(&out, "created: %zu bytes", new_len);
        else sb_puts(&out, "error: write failed; file may be partially written");
        return sb_detach(&out);
    }

    /* 文件存在却传空 old_string：几乎可以肯定是模型想覆盖整个文件，
     * 而覆盖正是这个工具被刻意禁止的事。拒绝并提示它走编辑路径。 */
    if (old_len == 0) {
        fclose(fp);
        sb_puts(&out, "error: file already exists; provide old_string to edit it");
        return sb_detach(&out);
    }

    /* 编辑前需要完整原文。这里没有 4000 字节上限——那是"给模型看的
     * 输出"的预算；定位编辑点必须看到全文件，否则替换会破坏截断之后
     * 的内容（正是整文件重写会犯的错）。 */
    strbuf content;
    sb_init(&content);
    int ok = read_all(fp, &content);
    fclose(fp);
    if (!ok) {
        sb_free(&content);
        sb_puts(&out, "error: file could not be read");
        return sb_detach(&out);
    }

    size_t pos = 0;
    size_t count = count_occurrences(content.data, content.len, old_s, old_len, &pos);
    if (count == 0) {
        sb_free(&content);
        sb_puts(&out, "error: old_string not found in file");
        return sb_detach(&out);
    }
    if (count > 1) {
        sb_free(&content);
        sb_printf(&out, "error: old_string matches %zu locations; add surrounding context to make it unique", count);
        return sb_detach(&out);
    }

    /* 前段 + 新文本 + 后段。三段都可能为空，sb_write 对 0 长度是安全的。 */
    strbuf merged;
    sb_init(&merged);
    ok = sb_write(&merged, content.data, pos) &&
         sb_write(&merged, new_s, new_len) &&
         sb_write(&merged, content.data + pos + old_len, content.len - pos - old_len);
    sb_free(&content);
    if (!ok) {
        sb_free(&merged);
        sb_puts(&out, "error: out of memory");
        return sb_detach(&out);
    }

    FILE *wf = file_open(path->valuestring, L"wb");
    if (!wf) {
        sb_free(&merged);
        sb_puts(&out, "error: cannot write file");
        return sb_detach(&out);
    }
    ok = merged.len == 0 || fwrite(merged.data, 1, merged.len, wf) == merged.len;
    if (fclose(wf) != 0) ok = 0;
    if (ok) sb_printf(&out, "edited: replaced %zu bytes with %zu bytes", old_len, new_len);
    else sb_puts(&out, "error: write failed; file may be partially written");
    sb_free(&merged);
    return sb_detach(&out);
}

const tool_definition tool_write_file_definition = {
    .name = "write_file",
    .description = "编辑工作区文本文件：把文件中唯一出现的 old_string 替换为 new_string。old_string 为空且文件不存在时创建新文件。不做整文件覆盖。",
    .parameters_json = "{\"type\":\"object\",\"properties\":{"
        "\"path\":{\"type\":\"string\",\"description\":\"工作区相对文件路径\"},"
        "\"old_string\":{\"type\":\"string\",\"description\":\"要替换的原文，必须在文件中唯一；为空且文件不存在时表示新建\"},"
        "\"new_string\":{\"type\":\"string\",\"description\":\"替换后的文本，可为空串（即删除 old_string）\"}},"
        "\"required\":[\"path\",\"old_string\",\"new_string\"]}",
    .execute = write_file
};
