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

/* 创建/覆盖普通文件；不创建父目录，拒绝链接和多重硬链接。 */
FILE *permissions_open_write(const char *path);

/* 逐项枚举目录。visit 返回 0 时停止；成功（含提前停止）返回 1。 */
typedef int (*permissions_dir_visitor)(const char *name, int is_dir, void *context);
int permissions_list_dir(const char *path, permissions_dir_visitor visit, void *context);

/* 返回初始化时的工作区绝对路径（UTF-8），调用方 free。供启动子进程使用。 */
char *permissions_workspace_path(void);

#ifndef _WIN32
/* 仅在 fork 后的命令子进程调用，按固定目录句柄切换工作目录。 */
int permissions_enter_workspace(void);
#endif

#endif
