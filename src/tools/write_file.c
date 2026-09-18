#include <string.h>
#include "tool_definition.h"
#include "permissions.h"
#include "strbuf.h"

static char *write_file(const cJSON *args) {
    strbuf out;
    sb_init(&out);
    const cJSON *path = cJSON_GetObjectItemCaseSensitive(args, "path");
    const cJSON *content = cJSON_GetObjectItemCaseSensitive(args, "content");
    if (!cJSON_IsString(path) || !cJSON_IsString(content)) {
        sb_puts(&out, "error: required string parameters \"path\" and \"content\"");
        return sb_detach(&out);
    }
    FILE *fp = permissions_open_write(path->valuestring);
    if (!fp) {
        sb_puts(&out, "error: file denied or cannot be written (workspace files only; parent directory must exist)");
        return sb_detach(&out);
    }
    size_t size = strlen(content->valuestring);
    int ok = fwrite(content->valuestring, 1, size, fp) == size;
    if (fclose(fp) != 0) ok = 0;
    if (ok) sb_printf(&out, "written: %zu bytes", size);
    else sb_puts(&out, "error: write failed; file may be partially written");
    return sb_detach(&out);
}

const tool_definition tool_write_file_definition = {
    .name = "write_file",
    .description = "创建或覆盖工作区文本文件，父目录必须存在。content 可以为空字符串。",
    .parameters_json = "{\"type\":\"object\",\"properties\":{\"path\":{\"type\":\"string\",\"description\":\"工作区相对文件路径\"},\"content\":{\"type\":\"string\",\"description\":\"完整文件内容，覆盖原内容\"}},\"required\":[\"path\",\"content\"]}",
    .execute = write_file
};
