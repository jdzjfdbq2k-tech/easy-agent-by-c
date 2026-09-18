#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "strbuf.h"

#define SB_MIN_CAP 64

static int sb_ensure(strbuf *sb, size_t extra) {
    if (sb->cap >= sb->len + extra + 1) return 1;

    size_t need = sb->len + extra + 1;
    size_t cap = sb->cap ? sb->cap : SB_MIN_CAP;

    while (cap < need) {
        if (cap > (size_t)-1 / 2) {  /* 溢出保护，理论上一辈子遇不到 */
            cap = need;
            break;
        }
        cap *= 2;
    }

    char *p = realloc(sb->data, cap);
    if (!p) return 0;  /* 原缓冲仍然有效，调用方可以决定怎么处理 */

    sb->data = p;
    sb->cap = cap;
    return 1;
}

void sb_init(strbuf *sb) {
    sb->data = NULL;
    sb->len = 0;
    sb->cap = 0;
}

void sb_free(strbuf *sb) {
    free(sb->data);
    sb_init(sb);
}

int sb_putc(strbuf *sb, char c) {
    if (!sb_ensure(sb, 1)) return 0;

    sb->data[sb->len++] = c;
    sb->data[sb->len] = '\0';
    return 1;
}

int sb_puts(strbuf *sb, const char *s) {
    if (!s) return 1;  /* 拼一个空指针当没拼，不算错 */

    size_t n = strlen(s);
    if (n == 0) return 1;

    if (!sb_ensure(sb, n)) return 0;

    memcpy(sb->data + sb->len, s, n);
    sb->len += n;
    sb->data[sb->len] = '\0';
    return 1;
}

int sb_write(strbuf *sb, const char *data, size_t n) {
    if (n == 0) return 1;
    if (!data) return 0;

    if (!sb_ensure(sb, n)) return 0;

    memcpy(sb->data + sb->len, data, n);
    sb->len += n;
    sb->data[sb->len] = '\0';
    return 1;
}

int sb_printf(strbuf *sb, const char *fmt, ...) {
    va_list ap;
    va_start(ap, fmt);

    /* 先用 vsnprintf(NULL, 0, ...) 问一次"格式化完有多长"，
     * 再按这个长度扩容。这样只需要一次写入，不用试探性 buf。 */
    va_list probe;
    va_copy(probe, ap);
    int n = vsnprintf(NULL, 0, fmt, probe);
    va_end(probe);

    if (n < 0) {
        va_end(ap);
        return 0;
    }

    if (!sb_ensure(sb, (size_t)n)) {
        va_end(ap);
        return 0;
    }

    vsnprintf(sb->data + sb->len, (size_t)n + 1, fmt, ap);
    va_end(ap);

    sb->len += (size_t)n;
    return 1;
}

char *sb_detach(strbuf *sb) {
    char *p = sb->data;

    if (!p) {
        /* 从没写过东西：给调用方一块合法的空字符串，别返回 NULL，
         * 否则每个调用点都要判空，很容易漏。 */
        p = malloc(1);
        if (p) p[0] = '\0';
    }

    sb_init(sb);
    return p;
}
