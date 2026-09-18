#ifndef TOOL_DEFINITION_H
#define TOOL_DEFINITION_H

#include <stddef.h>
#include "cJSON.h"

/* 工具作者接口，仅供工具层使用。元数据具有静态生命周期。
 * execute 借用 args，不可保存或释放它；返回 malloc 分配的文本，
 * 调用方负责 free，内存不足可返回 NULL。字段语义由各工具校验。
 */
typedef struct {
    const char *name;
    const char *description;
    const char *parameters_json;
    char *(*execute)(const cJSON *args);
} tool_definition;

/* builtins.c 是唯一注册入口；schema 和执行分发共用这张表。 */
extern const tool_definition *const builtin_tools[];
extern const size_t builtin_tools_count;

#endif
