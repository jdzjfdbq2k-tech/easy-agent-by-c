#include <string.h>
#include "tool_definition.h"
#include "file_io.h"
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

static int list_entries(const char *path, strbuf *out) {
    wchar_t *wide = file_path_wide(path);
    if (!wide) return 0;
    DWORD attrs = GetFileAttributesW(wide);
    if (attrs == INVALID_FILE_ATTRIBUTES || !(attrs & FILE_ATTRIBUTE_DIRECTORY)) {
        free(wide);
        return 0;
    }
    size_t len = wcslen(wide);
    wchar_t *pattern = realloc(wide, (len + 3) * sizeof(*wide));
    if (!pattern) { free(wide); return 0; }
    if (len && pattern[len - 1] != L'/' && pattern[len - 1] != L'\\') pattern[len++] = L'\\';
    pattern[len++] = L'*';
    pattern[len] = L'\0';
    WIN32_FIND_DATAW data;
    HANDLE find = FindFirstFileW(pattern, &data);
    free(pattern);
    if (find == INVALID_HANDLE_VALUE) return GetLastError() == ERROR_FILE_NOT_FOUND;
    int ok = 1;
    do {
        if (!wcscmp(data.cFileName, L".") || !wcscmp(data.cFileName, L"..")) continue;
        char name[4 * MAX_PATH];
        if (!WideCharToMultiByte(CP_UTF8, WC_ERR_INVALID_CHARS, data.cFileName,
                                -1, name, sizeof(name), NULL, NULL)) { ok = 0; break; }
        if (!entry(name, !!(data.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY), out)) {
            FindClose(find);
            return 1;
        }
    } while (FindNextFileW(find, &data));
    if (ok) ok = GetLastError() == ERROR_NO_MORE_FILES;
    FindClose(find);
    return ok;
}

static char *list_dir(const cJSON *args) {
    strbuf out;
    sb_init(&out);
    const cJSON *path = cJSON_GetObjectItemCaseSensitive(args, "path");
    if (!cJSON_IsString(path))
        sb_puts(&out, "error: missing required string parameter \"path\"");
    else if (!list_entries(path->valuestring, &out)) {
        sb_free(&out);
        sb_init(&out);
        sb_puts(&out, "error: cannot list directory");
    } else if (!out.len) sb_puts(&out, "(empty directory)");
    return sb_detach(&out);
}

const tool_definition tool_list_dir_definition = {
    .name = "list_dir",
    .description = "列出工作区目录的直接子项，目录名以 / 结尾；输出最多约 4000 字节。",
    .parameters_json = "{\"type\":\"object\",\"properties\":{\"path\":{\"type\":\"string\",\"description\":\"工作区相对目录，例如 . 或 src\"}},\"required\":[\"path\"]}",
    .execute = list_dir
};
