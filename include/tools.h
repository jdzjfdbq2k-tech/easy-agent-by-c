#ifndef TOOLS_H
#define TOOLS_H

typedef struct {
    char *id;         /* 模型分配的调用 id，回填结果时必须原样带上 */
    char *name;       /* 工具名，如 read_file */
    char *arguments;  /* 参数 JSON 的文本形式，可能为 NULL */
} tool_call;

/* 释放一个 tool_call 内部的三个字符串，并把字段置空。可重复调用。 */
void tool_call_free(tool_call *call);

/* 返回给模型的工具定义，是一段 JSON **数组**文本（可能不止一个工具）。
 * 调用方负责 free；注册定义无效或内存不足时返回 NULL。 */
char *tools_schema_json(void);

/* 返回工具结果或 error: 文本，由调用方 free；内存不足可返回 NULL。 */
char *tool_run(const char *name, const char *arguments_json);

#endif
