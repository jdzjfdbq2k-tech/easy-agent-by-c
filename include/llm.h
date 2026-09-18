#ifndef LLM_H
#define LLM_H

#include "tools.h"

typedef struct {
    char      *content;         /* 普通回答文本，可能为 NULL */
    char      *assistant_json;  /* choices[0].message 的原始 JSON 文本，用于回填历史 */
    tool_call *calls;           /* 模型请求的工具调用数组，长度见 call_count */
    int        call_count;      /* 0 表示没有工具调用 */
    int        input_tokens;   /* usage 未提供时为 -1 */
    int        output_tokens;
} llm_result;

/* 发送完整历史；失败返回 NULL，成功结果用 llm_result_free 释放。 */
llm_result *call_llm(const char *messages_json);

/* 释放结果。可以安全地传 NULL。 */
void llm_result_free(llm_result *r);

#endif
