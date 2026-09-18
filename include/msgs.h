#ifndef MSGS_H
#define MSGS_H

typedef struct msgs msgs;

/* 历史持有消息副本；传入字符串仍由调用方管理。 */

/* 新建一个空的对话。失败返回 NULL。 */
msgs *msgs_new(void);

/* 释放对话及其内部所有消息。可以安全地传 NULL。 */
void msgs_free(msgs *m);

/* 追加固定系统指令；应在用户消息之前调用。 */
void msgs_add_system(msgs *m, const char *text);

void msgs_add_user(msgs *m, const char *text);

/* 追加完整 assistant JSON，保留工具调用等字段。 */
void msgs_add_assistant_raw(msgs *m, const char *message_json);

/* tool_call_id 必须对应模型请求的工具调用。 */
void msgs_add_tool_result(msgs *m, const char *tool_call_id, const char *output);

/* 返回 JSON 数组文本，由调用方 free；NULL 历史视为空数组，分配失败返回 NULL。 */
char *msgs_to_json(const msgs *m);

/* 当前有多少条消息。主要给调试和断言用。 */
int msgs_count(const msgs *m);

#endif
