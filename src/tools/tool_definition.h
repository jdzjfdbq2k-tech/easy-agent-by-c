#ifndef TOOL_DEFINITION_H
#define TOOL_DEFINITION_H

#include <stddef.h>
#include "cJSON.h"

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
