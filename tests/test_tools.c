/*
 * 平台无关的单元测试：覆盖 strbuf.c / tools.c / msgs.c。
 *
 * 这三个文件不碰 Windows API，所以在 macOS / Linux 上也能直接编译运行。
 * 它的价值在于：你选了 Windows 原生路线（WinHTTP），本机跑不了 agent.exe，
 * 但 agent 里最容易写错的那部分逻辑（字符串增长、JSON 转义、参数解析、
 * 输出截断、历史累积）全在这里，可以当场验穿。
 *
 * 必须在仓库根目录运行，因为用例里用了 src/main.c 这类相对路径。
 *   make -C tests
 */

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

    char *out = tool_run("read_file", "{\"path\":\"src/main.c\"}");
    CHECK(out != NULL, "returns a string (never NULL)");
    CHECK(out && strstr(out, "load_env") != NULL, "content really is main.c");
    CHECK(out && strstr(out, "include <stdio.h>") != NULL, "content starts correctly");
    free(out);

    out = tool_run("read_file", "{ \"path\" : \"src/llm.c\" }");
    CHECK(out && strstr(out, "parse_response") != NULL, "whitespace in arguments tolerated");
    free(out);
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

    const char *schema = tools_schema_json();
    CHECK(schema != NULL, "schema is not NULL");
    CHECK(schema && schema[0] == '[', "schema is a JSON array");

    int depth = 0, in_string = 0, ok = 1;
    for (const char *p = schema; p && *p; p++) {
        if (*p == '"' && (p == schema || *(p - 1) != '\\')) in_string = !in_string;
        if (in_string) continue;
        if (*p == '{' || *p == '[') depth++;
        if (*p == '}' || *p == ']') depth--;
        if (depth < 0) { ok = 0; break; }
    }
    CHECK(ok && depth == 0 && !in_string, "braces and brackets are balanced");

    CHECK(strstr(schema, "read_file") != NULL, "declares read_file");
    CHECK(strstr(schema, "\"required\"") != NULL, "declares required params");
}

static void test_msgs(void) {
    printf("== msgs ==\n");

    /* ---- 契约：空历史、NULL ---- */
    char *json = msgs_to_json(NULL);
    CHECK(json && strcmp(json, "[]") == 0, "msgs_to_json(NULL) 返回 \"[]\" 而不是 NULL");
    free(json);

    msgs *m = msgs_new();
    CHECK(m != NULL, "msgs_new 成功");
    CHECK(msgs_count(m) == 0, "新对话是空的");

    json = msgs_to_json(m);
    CHECK(json && strcmp(json, "[]") == 0, "空对话序列化成 \"[]\"");
    free(json);

    /* ---- 转义自证 ----
     * 内容里故意塞进双引号、反斜杠、换行，以及一段长得像 JSON 的文本。
     * 往返一圈之后必须一字不差。这一个用例同时验证了转义、拼接和括号闭合。 */
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

    /* ---- 顺序断言 ----
     * user -> assistant(带 tool_calls) -> tool，这是 OpenAI 协议的硬要求，
     * 顺序反了接口会返回 400。 */
    cJSON *m0 = cJSON_GetArrayItem(root, 0);
    cJSON *m1 = cJSON_GetArrayItem(root, 1);
    cJSON *m2 = cJSON_GetArrayItem(root, 2);

    cJSON *r0 = cJSON_GetObjectItem(m0, "role");
    cJSON *r1 = cJSON_GetObjectItem(m1, "role");
    cJSON *r2 = cJSON_GetObjectItem(m2, "role");

    CHECK(cJSON_IsString(r0) && strcmp(r0->valuestring, "user") == 0, "第 1 条是 user");
    CHECK(cJSON_IsString(r1) && strcmp(r1->valuestring, "assistant") == 0, "第 2 条是 assistant");
    CHECK(cJSON_IsString(r2) && strcmp(r2->valuestring, "tool") == 0, "第 3 条是 tool");

    /* ---- 转义往返 ---- */
    cJSON *c0 = cJSON_GetObjectItem(m0, "content");
    CHECK(cJSON_IsString(c0) && strcmp(c0->valuestring, nasty) == 0,
          "user 内容里的引号 / 反斜杠 / 换行一字不差地活了下来");

    cJSON *c2 = cJSON_GetObjectItem(m2, "content");
    CHECK(cJSON_IsString(c2) && strcmp(c2->valuestring, nasty) == 0,
          "tool 结果（真实场景里是文件内容）同样一字不差");

    cJSON *id2 = cJSON_GetObjectItem(m2, "tool_call_id");
    CHECK(cJSON_IsString(id2) && strcmp(id2->valuestring, "call_1") == 0,
          "tool_call_id 原样保留");

    /* ---- 嵌套结构没被压平 ---- */
    cJSON *tc = cJSON_GetObjectItem(m1, "tool_calls");
    CHECK(cJSON_IsArray(tc) && cJSON_GetArraySize(tc) == 1, "assistant 的 tool_calls 完整保留");

    cJSON *fn = cJSON_GetObjectItem(cJSON_GetArrayItem(tc, 0), "function");
    cJSON *args = cJSON_GetObjectItem(fn, "arguments");
    CHECK(cJSON_IsString(args) && strcmp(args->valuestring, "{\"path\":\"a.c\"}") == 0,
          "嵌套的 arguments 还是一段字符串，没有被展开成对象");

    cJSON_Delete(root);
    free(json);

    /* ---- 非法 JSON 被拦下 ---- */
    int before = msgs_count(m);
    msgs_add_assistant_raw(m, "not json at all");
    CHECK(msgs_count(m) == before, "非法 JSON 被拒绝，count 不变");

    /* ---- 扩容路径 ---- */
    for (int i = 0; i < 100; i++) msgs_add_user(m, "x");
    CHECK(msgs_count(m) == 103, "扩容路径：累计 103 条");

    json = msgs_to_json(m);
    root = cJSON_Parse(json);
    CHECK(root && cJSON_GetArraySize(root) == 103, "103 条全部序列化成功");
    cJSON_Delete(root);
    free(json);

    /* ---- NULL 安全 ---- */
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

int main(void) {
    test_strbuf_basic();
    test_strbuf_edge();
    test_strbuf_growth();
    test_read_file();
    test_read_file_errors();
    test_truncation();
    test_truncation_utf8();
    test_schema();
    test_msgs();

    printf("\n");
    if (failures == 0) {
        printf("ALL PASS\n");
    } else {
        printf("%d FAILURE(S)\n", failures);
    }
    return failures == 0 ? 0 : 1;
}
