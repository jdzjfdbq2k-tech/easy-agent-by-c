

#include <direct.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "cJSON.h"
#include "msgs.h"
#include "strbuf.h"
#include "tools.h"

static int failures = 0;

#define CHECK(cond, msg)                                    \
    do {                                                    \
        if (cond) {                                         \
            printf("  PASS  %s\n", msg);                    \
        } else {                                            \
            printf("  FAIL  %s\n", msg);                    \
            failures++;                                     \
        }                                                   \
    } while (0)

static void test_strbuf_basic(void) {
    printf("== strbuf: basic ==\n");

    strbuf sb;
    sb_init(&sb);

    CHECK(sb_puts(&sb, "hello"), "puts on empty buffer");
    CHECK(sb_putc(&sb, ' '), "putc");
    CHECK(sb_puts(&sb, "world"), "puts again");
    CHECK(sb.len == 11, "len == 11");
    CHECK(sb.data && strcmp(sb.data, "hello world") == 0, "contents correct");

    CHECK(sb_printf(&sb, " %d%c%s", 42, '!', "z"), "printf append");
    CHECK(sb.data && strcmp(sb.data, "hello world 42!z") == 0, "printf contents correct");

    char *p = sb_detach(&sb);
    CHECK(p && strcmp(p, "hello world 42!z") == 0, "detach transfers ownership");
    CHECK(sb.data == NULL && sb.len == 0 && sb.cap == 0, "strbuf reset after detach");
    free(p);

    sb_free(&sb);
}

static void test_strbuf_edge(void) {
    printf("== strbuf: edge cases ==\n");

    strbuf sb;
    sb_init(&sb);

    char *empty = sb_detach(&sb);
    CHECK(empty && empty[0] == '\0', "detach on never-written buffer yields empty string");
    free(empty);

    CHECK(sb_puts(&sb, NULL), "puts(NULL) is a no-op, not an error");
    CHECK(sb.len == 0, "len still 0 after puts(NULL)");

    CHECK(sb_puts(&sb, ""), "puts(\"\") succeeds");
    CHECK(sb_write(&sb, "", 0), "write of 0 bytes succeeds");

    sb_free(&sb);
}

static void test_strbuf_growth(void) {
    printf("== strbuf: growth ==\n");

    strbuf sb;
    sb_init(&sb);

    for (int i = 0; i < 10000; i++) {
        if (!sb_puts(&sb, "0123456789")) {
            printf("  FAIL  allocation failed at iteration %d\n", i);
            failures++;
            sb_free(&sb);
            return;
        }
    }

    CHECK(sb.len == 100000, "grew to 100000 bytes");
    CHECK(sb.data[99999] == '9', "last byte correct");
    CHECK(sb.data[100000] == '\0', "terminator intact after many reallocs");

    printf("  info  final capacity %zu for len 100000 -> overshoot %.0f%%\n",
           sb.cap, 100.0 * ((double)sb.cap / 100000.0 - 1.0));

    sb_free(&sb);
}

static void test_read_file(void) {
    printf("== tool_run: read_file ==\n");

    /* 工作区固定在 workspace/ 子目录，夹具也必须落在那里面，
     * 再通过工具自己的相对路径读回来。 */
    FILE *fp = fopen("tmp_read.c", "wb");
    if (!fp) {
        printf("  FAIL  could not create test fixture\n");
        failures++;
        return;
    }
    fputs("#include <stdio.h>\nint main(void) { load_env(\".env\"); return 0; }\n", fp);
    fclose(fp);

    char *out = tool_run("read_file", "{\"path\":\"tmp_read.c\"}");
    CHECK(out != NULL, "returns a string (never NULL)");
    CHECK(out && strstr(out, "load_env") != NULL, "content really round trips");
    CHECK(out && strstr(out, "#include <stdio.h>") != NULL, "content starts correctly");
    free(out);

    out = tool_run("read_file", "{ \"path\" : \"tmp_read.c\" }");
    CHECK(out && strstr(out, "load_env") != NULL, "whitespace in arguments tolerated");
    free(out);

    remove("tmp_read.c");
}

/* Paths are ordinary OS paths; all fixtures stay in our test directory. */
static void test_plain_paths(void) {
    _mkdir("tmp_paths");
    char *out = tool_run("write_file", "{\"path\":\"tmp_paths/../tmp_paths/plain.txt\",\"old_string\":\"\",\"new_string\":\"plain paths\"}");
    CHECK(out && strstr(out, "created:"), "write_file accepts parent path segments");
    free(out);

    char *absolute = _fullpath(NULL, "tmp_paths/plain.txt", 0);
    cJSON *args = cJSON_CreateObject();
    if (absolute) cJSON_AddStringToObject(args, "path", absolute);
    char *json = cJSON_PrintUnformatted(args);
    out = tool_run("read_file", json);
    CHECK(out && strcmp(out, "plain paths") == 0, "read_file accepts absolute paths");
    free(out); free(absolute); cJSON_free(json); cJSON_Delete(args);

    out = tool_run("list_dir", "{\"path\":\"tmp_paths/..\"}");
    CHECK(out && strstr(out, "tmp_paths/"), "list_dir accepts parent path segments");
    free(out);
    remove("tmp_paths/plain.txt");
    _rmdir("tmp_paths");
}

static void test_read_file_errors(void) {
    printf("== tool_run: error paths ==\n");

    char *out = tool_run("read_file", "{}");
    CHECK(out && strncmp(out, "error:", 6) == 0, "missing path -> error string");
    CHECK(out && strstr(out, "path") != NULL, "error names the missing parameter");
    free(out);

    out = tool_run("read_file", "{\"path\":\"no/such/file.txt\"}");
    CHECK(out && strncmp(out, "error:", 6) == 0, "nonexistent file -> error string");
    free(out);

    out = tool_run("read_file", "{\"path\": 12345}");
    CHECK(out && strncmp(out, "error:", 6) == 0, "wrong param type -> error string");
    free(out);

    out = tool_run("no_such_tool", "{}");
    CHECK(out && strstr(out, "unknown tool") != NULL, "unknown tool -> error string");
    free(out);

    out = tool_run("read_file", "this is not json");
    CHECK(out && strstr(out, "not valid JSON") != NULL, "garbage arguments -> error string");
    free(out);

    out = tool_run("read_file", "");
    CHECK(out && strncmp(out, "error:", 6) == 0, "empty arguments treated as {}");
    free(out);

    out = tool_run("read_file", NULL);
    CHECK(out && strncmp(out, "error:", 6) == 0, "NULL arguments treated as {}");
    free(out);

    out = tool_run(NULL, "{}");
    CHECK(out && strncmp(out, "error:", 6) == 0, "NULL tool name -> error string");
    free(out);
}

static void test_truncation(void) {
    printf("== tool_run: truncation ==\n");

    const char *big = "tmp_big.txt";
    FILE *fp = fopen(big, "wb");
    if (!fp) {
        printf("  FAIL  could not create test fixture\n");
        failures++;
        return;
    }
    for (int i = 0; i < 10000; i++) fputc('A', fp);
    fclose(fp);

    char *out = tool_run("read_file", "{\"path\":\"tmp_big.txt\"}");
    CHECK(out != NULL, "big file returns a string");

    if (out) {
        size_t n = strlen(out);
        printf("  info  10000-byte file came back as %zu bytes\n", n);
        CHECK(n < 4200, "output was capped near TOOL_OUTPUT_MAX (4000)");
        CHECK(strstr(out, "truncated") != NULL, "truncation is announced in the text");
    }
    free(out);
    remove(big);

    const char *empty = "tmp_empty.txt";
    fp = fopen(empty, "wb");
    if (fp) fclose(fp);

    out = tool_run("read_file", "{\"path\":\"tmp_empty.txt\"}");
    CHECK(out && strcmp(out, "(empty file)") == 0, "empty file gets an explicit marker, not \"\"");
    free(out);
    remove(empty);
}

/* 极简 UTF-8 校验器：逐字符走一遍，非法序列返回 0。
 * 这里不追求功能完整，只要能识别"切在多字节字符中间"这一种毛病就够了。 */
static int utf8_valid(const char *s) {
    const unsigned char *p = (const unsigned char *)s;
    size_t n = strlen(s);
    size_t i = 0;

    while (i < n) {
        unsigned char c = p[i];
        size_t len;

        if      (c < 0x80)          len = 1;
        else if ((c & 0xE0) == 0xC0) len = 2;
        else if ((c & 0xF0) == 0xE0) len = 3;
        else if ((c & 0xF8) == 0xF0) len = 4;
        else return 0;                      /* 非法首字节 */

        if (i + len > n) return 0;          /* 字符被切断了 */

        for (size_t k = 1; k < len; k++) {
            if ((p[i + k] & 0xC0) != 0x80) return 0;
        }
        i += len;
    }
    return 1;
}

static void test_truncation_utf8(void) {
    printf("== tool_run: 截断不能切在多字节字符中间 ==\n");

    /* 造一个文件，让第 4000 个字节正好落在某个中文字符的中间：
     * 1334 个 'A'（1334 字节）+ 若干个三字节的中文，
     * 4000 - 1334 = 2666 = 3*888 + 2，也就是切在第 889 个中文字的中间。 */
    const char *path = "tmp_utf8.txt";
    FILE *fp = fopen(path, "wb");
    if (!fp) {
        printf("  FAIL  could not create test fixture\n");
        failures++;
        return;
    }
    for (int i = 0; i < 1334; i++) fputc('A', fp);
    for (int i = 0; i < 1000; i++) fputs("中", fp);
    fclose(fp);

    char *out = tool_run("read_file", "{\"path\":\"tmp_utf8.txt\"}");
    CHECK(out != NULL, "读回来了");

    if (out) {
        size_t n = strlen(out);
        printf("  info  输出 %zu 字节\n", n);
        CHECK(strstr(out, "truncated") != NULL, "确实发生了截断");
        CHECK(utf8_valid(out), "截断后的输出仍然是合法 UTF-8（没有半个汉字）");
        CHECK(n >= 4000 && n < 4100, "正文保留在 4000 字节附近，没有因为回退丢太多");
    }

    free(out);
    remove(path);
}

static void test_schema(void) {
    printf("== tools_schema_json ==\n");

    char *schema = tools_schema_json();
    CHECK(schema != NULL, "schema is not NULL");
    CHECK(schema && schema[0] == '[', "schema is a JSON array");

    cJSON *root = schema ? cJSON_Parse(schema) : NULL;
    CHECK(cJSON_IsArray(root), "schema is valid JSON array");
    CHECK(cJSON_GetArraySize(root) == 4, "four built-in tools registered");
    cJSON *fn = cJSON_GetObjectItem(cJSON_GetArrayItem(root, 0), "function");
    cJSON *name = cJSON_GetObjectItem(fn, "name");
    CHECK(cJSON_IsString(name) && strcmp(name->valuestring, "read_file") == 0,
          "registered name is read_file");
    cJSON *params = cJSON_GetObjectItem(fn, "parameters");
    CHECK(cJSON_IsObject(params), "parameters is an object, not an encoded string");
    cJSON *required = cJSON_GetArrayItem(cJSON_GetObjectItem(params, "required"), 0);
    CHECK(cJSON_IsString(required) && strcmp(required->valuestring, "path") == 0,
          "path remains required");
    cJSON_Delete(root);
    free(schema);
}

static void test_msgs(void) {
    printf("== msgs ==\n");

    char *json = msgs_to_json(NULL);
    CHECK(json && strcmp(json, "[]") == 0, "msgs_to_json(NULL) 返回 \"[]\" 而不是 NULL");
    free(json);

    msgs *m = msgs_new();
    CHECK(m != NULL, "msgs_new 成功");
    CHECK(msgs_count(m) == 0, "新对话是空的");

    json = msgs_to_json(m);
    CHECK(json && strcmp(json, "[]") == 0, "空对话序列化成 \"[]\"");
    free(json);

    const char *nasty = "quote:\" back\\slash newline:\n brace:{} comma:, end";

    const char *assistant =
        "{\"role\":\"assistant\",\"content\":null,"
        "\"tool_calls\":[{\"id\":\"call_1\",\"type\":\"function\","
        "\"function\":{\"name\":\"read_file\",\"arguments\":\"{\\\"path\\\":\\\"a.c\\\"}\"}}]}";

    msgs_add_user(m, nasty);
    msgs_add_assistant_raw(m, assistant);
    msgs_add_tool_result(m, "call_1", nasty);
    CHECK(msgs_count(m) == 3, "三条消息都进去了");

    json = msgs_to_json(m);
    CHECK(json != NULL, "msgs_to_json 返回非 NULL");

    cJSON *root = cJSON_Parse(json);
    CHECK(root != NULL, "拼出来的文本能被重新 parse（方括号闭合、逗号正确）");
    CHECK(root && cJSON_IsArray(root), "它是一个 JSON 数组");
    CHECK(root && cJSON_GetArraySize(root) == 3, "数组里有 3 个元素");

    cJSON *m0 = cJSON_GetArrayItem(root, 0);
    cJSON *m1 = cJSON_GetArrayItem(root, 1);
    cJSON *m2 = cJSON_GetArrayItem(root, 2);

    cJSON *r0 = cJSON_GetObjectItem(m0, "role");
    cJSON *r1 = cJSON_GetObjectItem(m1, "role");
    cJSON *r2 = cJSON_GetObjectItem(m2, "role");

    CHECK(cJSON_IsString(r0) && strcmp(r0->valuestring, "user") == 0, "第 1 条是 user");
    CHECK(cJSON_IsString(r1) && strcmp(r1->valuestring, "assistant") == 0, "第 2 条是 assistant");
    CHECK(cJSON_IsString(r2) && strcmp(r2->valuestring, "tool") == 0, "第 3 条是 tool");

    cJSON *c0 = cJSON_GetObjectItem(m0, "content");
    CHECK(cJSON_IsString(c0) && strcmp(c0->valuestring, nasty) == 0,
          "user 内容里的引号 / 反斜杠 / 换行一字不差地活了下来");

    cJSON *c2 = cJSON_GetObjectItem(m2, "content");
    CHECK(cJSON_IsString(c2) && strcmp(c2->valuestring, nasty) == 0,
          "tool 结果（真实场景里是文件内容）同样一字不差");

    cJSON *id2 = cJSON_GetObjectItem(m2, "tool_call_id");
    CHECK(cJSON_IsString(id2) && strcmp(id2->valuestring, "call_1") == 0,
          "tool_call_id 原样保留");

    cJSON *tc = cJSON_GetObjectItem(m1, "tool_calls");
    CHECK(cJSON_IsArray(tc) && cJSON_GetArraySize(tc) == 1, "assistant 的 tool_calls 完整保留");

    cJSON *fn = cJSON_GetObjectItem(cJSON_GetArrayItem(tc, 0), "function");
    cJSON *args = cJSON_GetObjectItem(fn, "arguments");
    CHECK(cJSON_IsString(args) && strcmp(args->valuestring, "{\"path\":\"a.c\"}") == 0,
          "嵌套的 arguments 还是一段字符串，没有被展开成对象");

    cJSON_Delete(root);
    free(json);

    int before = msgs_count(m);
    msgs_add_assistant_raw(m, "not json at all");
    CHECK(msgs_count(m) == before, "非法 JSON 被拒绝，count 不变");

    for (int i = 0; i < 100; i++) msgs_add_user(m, "x");
    CHECK(msgs_count(m) == 103, "连续追加：累计 103 条");

    json = msgs_to_json(m);
    root = cJSON_Parse(json);
    CHECK(root && cJSON_GetArraySize(root) == 103, "103 条全部序列化成功");
    cJSON_Delete(root);
    free(json);

    msgs_add_user(NULL, "x");
    msgs_add_assistant_raw(NULL, "{}");
    msgs_add_tool_result(NULL, "id", "out");
    msgs_add_user(m, NULL);
    msgs_add_assistant_raw(m, NULL);
    msgs_add_tool_result(m, "id", NULL);
    CHECK(msgs_count(NULL) == 0, "msgs_count(NULL) 返回 0");
    msgs_free(NULL);
    CHECK(1, "传 NULL 全程不崩");

    msgs_free(m);
}

static void test_new_tools(void) {
    /* 子目录夹具：验证 list_dir 能看到嵌套结构 */
    _mkdir("tmp_sub");
    FILE *nested = fopen("tmp_sub/inside.txt", "wb");
    if (nested) {
        fputs("x", nested);
        fclose(nested);
    }

    FILE *bad_name = fopen("tmp_bad\xff", "wb");
    if (bad_name) {
        fclose(bad_name);
        char *listing = tool_run("list_dir", "{\"path\":\".\"}");
        CHECK(listing && utf8_valid(listing) && strstr(listing, "tmp_bad\\xff"), "directory listing escapes invalid UTF-8 bytes");
        free(listing);
        remove("tmp_bad\xff");
    } else puts("  SKIP  filesystem does not support non-UTF-8 filenames");
    char *out = tool_run("list_dir", "{\"path\":\".\"}");
    CHECK(out && strstr(out, "tmp_sub/"), "list_dir marks directories with a trailing slash");
    free(out);
    out = tool_run("list_dir", "{\"path\":\"tmp_sub\"}");
    CHECK(out && strstr(out, "inside.txt"), "list_dir lists nested directories");
    free(out);

    out = tool_run("write_file", "{\"path\":\"tmp_write.txt\",\"old_string\":\"\",\"new_string\":\"中文\\nhello\"}");
    CHECK(out && !strstr(out, "error:"), "write_file creates a file with empty old_string"); free(out);
    out = tool_run("read_file", "{\"path\":\"tmp_write.txt\"}");
    CHECK(out && strcmp(out, "中文\nhello") == 0, "created content round trips"); free(out);
    out = tool_run("write_file", "{\"path\":\"tmp_write.txt\",\"old_string\":\"\",\"new_string\":\"x\"}");
    CHECK(out && strstr(out, "error:"), "empty old_string on an existing file is rejected"); free(out);
    out = tool_run("write_file", "{\"path\":\"tmp_write.txt\",\"old_string\":\"hello\",\"new_string\":\"world\"}");
    CHECK(out && !strstr(out, "error:"), "write_file edits only the matched part"); free(out);
    out = tool_run("read_file", "{\"path\":\"tmp_write.txt\"}");
    CHECK(out && strcmp(out, "中文\nworld") == 0, "the rest of the file is untouched"); free(out);
    out = tool_run("write_file", "{\"path\":\"tmp_write.txt\",\"old_string\":\"no such text\",\"new_string\":\"x\"}");
    CHECK(out && strstr(out, "error:"), "old_string not found is an error"); free(out);
    out = tool_run("write_file", "{\"path\":\"tmp_dup.txt\",\"old_string\":\"\",\"new_string\":\"ab\\nab\\n\"}");
    CHECK(out && !strstr(out, "error:"), "ambiguity fixture created"); free(out);
    out = tool_run("write_file", "{\"path\":\"tmp_dup.txt\",\"old_string\":\"ab\",\"new_string\":\"x\"}");
    CHECK(out && strstr(out, "error:"), "ambiguous old_string is rejected, not guessed"); free(out);
    out = tool_run("write_file", "{\"path\":\"tmp_write.txt\",\"old_string\":\"\\nworld\",\"new_string\":\"\"}");
    CHECK(out && !strstr(out, "error:"), "empty new_string deletes the match"); free(out);
    out = tool_run("read_file", "{\"path\":\"tmp_write.txt\"}");
    CHECK(out && strcmp(out, "中文") == 0, "deletion applied correctly"); free(out);
    remove("tmp_write.txt");
    remove("tmp_dup.txt");
    out = tool_run("run_command", "{\"command\":\"echo hello & echo problem 1>&2 & exit /b 7\"}");
    CHECK(out && strstr(out, "hello") && strstr(out, "problem") && strstr(out, "exit_code: 7"), "command captures stdout stderr and exit code"); free(out);
    out = tool_run("run_command", "{\"command\":\"ping -n 6 127.0.0.1 >nul\",\"timeout_ms\":300}");
    CHECK(out && strstr(out, "timed out") && strstr(out, "exit_code: 124"), "command enforces timeout"); free(out);
    /* 输出远超 4000 字节，验证上限生效且不会把内存读爆。 */
    out = tool_run("run_command", "{\"command\":\"for /l %i in (1,1,4000) do @echo AAAAAAAAAA\"}");
    CHECK(out && strlen(out) < 4300 && strstr(out, "truncated"), "command bounds continuous output"); free(out);

    out = tool_run("list_dir", "{\"path\":\"tmp_sub/\"}");
    CHECK(out && strstr(out, "inside.txt"), "list_dir accepts trailing separator"); free(out);
    /* 清洗后的命令输出必须始终是合法 UTF-8。
     * 这里只断言"清洗生效"，不追中文能不能原样回来 —— cmd.exe 内含的非 UTF-8
     * 字节（中文 Windows 上是 GBK）会被替换成 ?，属于已知限制。 */
    out = tool_run("run_command", "{\"command\":\"echo abc\"}");
    CHECK(out && strstr(out, "abc") && utf8_valid(out), "command output is always valid UTF-8"); free(out);
    out = tool_run("run_command", "{\"command\":\"set /p v=& echo stdin-closed\"}");
    CHECK(out && strstr(out, "stdin-closed") && strstr(out, "exit_code: 0"), "command stdin is closed for interactive reads"); free(out);
    out = tool_run("run_command", "{\"command\":\"echo x\",\"timeout_ms\":0}");
    CHECK(out && strstr(out, "error:"), "command rejects invalid timeout"); free(out);
    /* 332 行 * 12 字节 + 一行 16 字节 = 正好 4000 字节：等于上限时不该报截断。 */
    out = tool_run("run_command", "{\"command\":\"(for /l %i in (1,1,332) do @echo AAAAAAAAAA) & echo AAAAAAAAAAAAAA\"}");
    CHECK(out && !strstr(out, "truncated") && strlen(out) > 4000, "exact output limit does not falsely truncate"); free(out);
    const char *names[] = {"list_dir", "write_file", "run_command"};
    for (size_t i = 0; i < 3; i++) {
        out = tool_run(names[i], "{\"path\":12,\"command\":false,\"old_string\":0,\"new_string\":false}");
        CHECK(out && strstr(out, "error:"), "new tools validate argument types"); free(out);
    }
    remove("tmp_sub/inside.txt");
    _rmdir("tmp_sub");
}

int main(void) {
    _mkdir("workspace");
    if (_chdir("workspace") != 0) return 1;
    test_strbuf_basic();
    test_strbuf_edge();
    test_strbuf_growth();
    test_read_file();
    test_plain_paths();
    test_read_file_errors();
    test_truncation();
    test_truncation_utf8();
    test_schema();
    test_new_tools();
    test_msgs();

    printf("\n");
    if (failures == 0) {
        printf("ALL PASS\n");
    } else {
        printf("%d FAILURE(S)\n", failures);
    }

    fflush(stdout);
    return failures == 0 ? 0 : 1;
}
