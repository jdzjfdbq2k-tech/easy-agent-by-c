#ifndef TOOL_TEXT_OUTPUT_H
#define TOOL_TEXT_OUTPUT_H
#include <stddef.h>

/* 返回首个完整 UTF-8 字符的字节数；非法或不完整序列返回 0。 */
static inline size_t tool_utf8_char_size(const unsigned char *s, size_t available) {
    if (!available) return 0;
    unsigned char c = s[0];
    size_t n = c < 0x80 ? 1 : c >= 0xc2 && c <= 0xdf ? 2 :
               c >= 0xe0 && c <= 0xef ? 3 : c >= 0xf0 && c <= 0xf4 ? 4 : 0;
    if (!n || n > available) return 0;
    for (size_t i = 1; i < n; i++) if ((s[i] & 0xc0) != 0x80) return 0;
    if (n >= 3 && ((c == 0xe0 && s[1] < 0xa0) || (c == 0xed && s[1] >= 0xa0) ||
                  (c == 0xf0 && s[1] < 0x90) || (c == 0xf4 && s[1] >= 0x90))) return 0;
    return n;
}
#endif
