#ifndef STRBUF_H
#define STRBUF_H

#include <stddef.h>

typedef struct {
    char  *data;  /* 始终以 '\0' 结尾（只要曾成功写入过），可直接当 C 字符串用 */
    size_t len;   /* 已写入的字符数，不含结尾 '\0' */
    size_t cap;   /* data 实际分配到的字节数 */
} strbuf;

/* 使用前初始化；追加操作返回 1 表示成功，0 表示失败。 */
void sb_init(strbuf *sb);

/* 释放内部缓冲并重置为空。可以安全地对空 strbuf 调用。 */
void sb_free(strbuf *sb);

/* 拼接一个字符 / 一个 C 字符串 / 一段带格式的文本。 */
int sb_putc(strbuf *sb, char c);
int sb_puts(strbuf *sb, const char *s);
int sb_printf(strbuf *sb, const char *fmt, ...);

/* 追加 n 个字节的原始数据（中间可以含 '\0'，用于读文件）。 */
int sb_write(strbuf *sb, const char *data, size_t n);

/* 交出缓冲并重置；调用方 free。空缓冲返回空字符串，分配失败返回 NULL。 */
char *sb_detach(strbuf *sb);

#endif
