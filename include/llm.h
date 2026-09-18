#ifndef LLM_H
#define LLM_H

#include "tools.h"

/*
 * llm —— 把"一次模型调用"的结果完整带回来。
 *
 * 为什么不能只返回一个字符串：
 *   接入工具调用之后，模型的回复有两种形态，而且互斥：
 *     1) 普通回答   -> content 有值，call_count == 0
 *     2) 请求调工具 -> content 通常为 NULL，call_count > 0
 *   调用方必须能区分这两种情况，所以返回值是一个结构体。
 *
 * 内存所有权：
 *   call_llm 返回的 llm_result 由调用方用 llm_result_free 释放。
 *   三个字段都由结构体自己管，不要单独 free。
 */

typedef struct {
    char      *content;         /* 普通回答文本，可能为 NULL */
    char      *assistant_json;  /* choices[0].message 的原始 JSON 文本，用于回填历史 */
    tool_call *calls;           /* 模型请求的工具调用数组，长度见 call_count */
    int        call_count;      /* 0 表示没有工具调用 */
} llm_result;

/*
 * 发起一次模型调用。
 *
 *   messages_json  整个对话历史的 JSON **数组文本**，形如
 *                  [{"role":"user","content":"..."},{"role":"assistant",...}]
 *                  用 msgs_to_json() 得到。
 *
 * 注意参数语义变了：以前传的是用户这一句，现在传的是完整历史。
 * 本模块不解析消息内容，只负责把它挂到请求体上 —— 所以它对
 * "消息里有什么角色、有没有 tool_calls" 一无所知，也不需要知道。
 *
 * 失败返回 NULL，原因打到 stderr。
 */
llm_result *call_llm(const char *messages_json);

/* 释放结果。可以安全地传 NULL。 */
void llm_result_free(llm_result *r);

#endif
