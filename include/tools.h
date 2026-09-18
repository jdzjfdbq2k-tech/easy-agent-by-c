#ifndef TOOLS_H
#define TOOLS_H

/*
 * tools —— 工具层。
 *
 * 边界非常清楚：
 *   tools.h 对外只暴露"名字 + 参数 JSON 字符串"这种形态，
 *   刻意不暴露 cJSON 的类型。这样 llm.c 不需要知道工具是怎么实现的，
 *   工具的增删只涉及 src/tools/，不影响模型与主循环模块。
 */

/* 模型请求的一次工具调用。
 *
 * 注意 arguments：它是**字符串**，里面装的才是参数 JSON 文本。
 * 也就是 arguments == "{\"path\":\"src/main.c\"}"，
 * 用之前必须再 parse 一次。这是接工具调用最常见的坑。 */
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

/* 执行一次工具调用。
 *   name           工具名
 *   arguments_json 模型给的参数文本（就是 tool_call.arguments）
 *
 * 返回值：工具输出文本，调用方负责 free。
 * 一般错误返回以 "error: " 开头的描述；内存不足时可能返回 NULL。
 *
 * 为什么出错也要返回字符串而不是 NULL：
 *   因为这段文本最终要塞回对话给模型看。模型看到 "error: ..." 才知道
 *   自己参数写错了，可以自己改；如果返回 NULL，模型只会看到一片空白，
 *   然后原地重试同样的错误输入，你就得到一个转圈的死循环。 */
char *tool_run(const char *name, const char *arguments_json);

#endif
