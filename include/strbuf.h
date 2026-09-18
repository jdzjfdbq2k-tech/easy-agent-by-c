#ifndef STRBUF_H
#define STRBUF_H

#include <stddef.h>

/*
 * strbuf —— 一个会自己长大的字符串。
 *
 * 为什么需要它：
 *   拼 JSON、拼消息、读文件内容，都需要把很多段文本接起来。
 *   用 strcat 链的问题是你必须先知道总长度，否则就会溢出；
 *   用固定缓冲区的问题是总有一个大小上限，而模型返回的内容长度不可控。
 *
 * 内存所有权：
 *   strbuf 结构自体不持有堆内存，它只是持有 data 指针。
 *   data 由 sb_* 系列函数负责分配和增长。
 *   正常流程是：sb_init -> 一堆 sb_puts/sb_printf -> sb_detach -> free。
 *   sb_detach 之后 strbuf 被重置为空，调用方拿到 data 的所有权，必须 free。
 *   如果不用 detach，就用 sb_free 释放。
 *
 * 失败约定：
 *   所有返回 int 的函数，1 表示成功，0 表示内存分配失败。
 *   返回 0 时 strbuf 保持原状，不会变成半截字符串。
 */

typedef struct {
    char  *data;  /* 始终以 '\0' 结尾（只要曾成功写入过），可直接当 C 字符串用 */
    size_t len;   /* 已写入的字符数，不含结尾 '\0' */
    size_t cap;   /* data 实际分配到的字节数 */
} strbuf;

/* 初始化。对未初始化的 strbuf 调用是未定义行为，必须先调这个。 */
void sb_init(strbuf *sb);

/* 释放内部缓冲并重置为空。可以安全地对空 strbuf 调用。 */
void sb_free(strbuf *sb);

/* 拼接一个字符 / 一个 C 字符串 / 一段带格式的文本。 */
int sb_putc(strbuf *sb, char c);
int sb_puts(strbuf *sb, const char *s);
int sb_printf(strbuf *sb, const char *fmt, ...);

/* 追加 n 个字节的原始数据（中间可以含 '\0'，用于读文件）。 */
int sb_write(strbuf *sb, const char *data, size_t n);

/* 交出所有权。返回值由调用方负责 free；strbuf 自身被重置为空。
 * 即使 strbuf 从未写入过，也会返回一块可用的空字符串，不会返回 NULL。 */
char *sb_detach(strbuf *sb);

#endif
