#ifndef PERMISSIONS_H
#define PERMISSIONS_H

#include <stdio.h>

/* 启动时调用一次，将当前目录固定为工作区。成功返回 1；重复调用不改变根目录。 */
int permissions_init(void);

/* 只读打开工作区内的普通文件，调用方 fclose。
 * 仅接受相对路径，拒绝 ..、盘符和越界链接；未初始化或失败返回 NULL。
 * 文件工具应使用此入口，不要先检查路径再自行 fopen。
 */
FILE *permissions_open_read(const char *path);

#endif
