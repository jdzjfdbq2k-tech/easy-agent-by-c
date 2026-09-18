#include "tool_definition.h"

extern const tool_definition tool_read_file_definition;

const tool_definition *const builtin_tools[] = {
    &tool_read_file_definition,
};

const size_t builtin_tools_count = sizeof(builtin_tools) / sizeof(builtin_tools[0]);
