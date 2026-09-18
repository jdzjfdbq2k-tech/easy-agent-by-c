#include <string.h>
#include "tool_definition.h"
#include "permissions.h"
#include "strbuf.h"
#include "text_output.h"

static int entry(const char *name, int is_dir, void *context) {
    strbuf *out = context;
    strbuf escaped;
    sb_init(&escaped);
    size_t length = strlen(name);
    for (size_t i = 0; i < length;) {
        size_t n = tool_utf8_char_size((const unsigned char *)name + i, length - i);
        int ok;
        if (!n || (unsigned char)name[i] < 0x20 || name[i] == '\\') {
            ok = sb_printf(&escaped, "\\x%02x", (unsigned char)name[i++]);
        } else {
            ok = sb_write(&escaped, name + i, n);
            i += n;
        }
        if (!ok) { sb_free(&escaped); return 0; }
    }
    if (out->len + escaped.len + 2 > 4000) {
        sb_free(&escaped);
        sb_puts(out, "... [truncated]\n");
        return 0;
    }
    int ok = sb_printf(out, "%s%s\n", escaped.data ? escaped.data : "", is_dir ? "/" : "");
    sb_free(&escaped);
    return ok;
}

static char *list_dir(const cJSON *args) {
    strbuf out;
    sb_init(&out);
    const cJSON *path = cJSON_GetObjectItemCaseSensitive(args, "path");
    if (!cJSON_IsString(path))
        sb_puts(&out, "error: missing required string parameter \"path\"");
    else if (!permissions_list_dir(path->valuestring, entry, &out)) {
        sb_free(&out);
        sb_init(&out);
        sb_puts(&out, "error: directory denied or cannot be listed (workspace directories only)");
    } else if (!out.len) sb_puts(&out, "(empty directory)");
    return sb_detach(&out);
}

const tool_definition tool_list_dir_definition = {
    .name = "list_dir",
    .description = "列出工作区目录的直接子项，目录名以 / 结尾；输出最多约 4000 字节。",
    .parameters_json = "{\"type\":\"object\",\"properties\":{\"path\":{\"type\":\"string\",\"description\":\"工作区相对目录，例如 . 或 src\"}},\"required\":[\"path\"]}",
    .execute = list_dir
};
