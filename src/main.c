#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "env.h"
#include "llm.h"
#include "msgs.h"
#include "tools.h"
#include "permissions.h"

/*
 * 第二步：agent 循环。
 *
 * 和第一步的区别只有一句话：工具的执行结果会**回填**进对话历史，
 * 然后带着这份历史重新问一次模型。
 *
 * 于是模型终于能"看见"文件内容了 —— 它第一轮说"我要读 src/main.c"，
 * 第二轮就拿到了内容，第三轮才能告诉你第 5 行是什么。
 * 所谓 agent，本质上就是这一个 for 循环。
 *
 * 三个出口（缺一个就会出事）：
 *   1. 模型不再请求工具  -> 正常结束，打印回答
 *   2. 达到 MAX_ROUNDS   -> 防死循环
 *   3. 请求失败          -> 提前退出
 *
 * 内存约定：每一轮里 msgs_to_json 和 call_llm 的返回值都要释放，
 * 而且**两条分支都要放**。循环里的泄漏是累积的，跑十轮就是十倍。
 *
 * 注意：所有控制台输出都用英文。
 * 原因：Windows 控制台默认代码页是 936（GBK），而源码是 UTF-8。
 * 直接 printf 中文会显示成乱码。中文放在注释里，以及送给模型的
 * 工具描述里（那段是走 HTTP 传的，UTF-8 完全没问题）。
 */

#define MAX_ROUNDS 20

int main(void) {
    if (!permissions_init()) {
        fprintf(stderr, "Failed to initialize workspace permissions\n");
        return 1;
    }

    if (!load_env(".env")) {
        fprintf(stderr, "Failed to load .env\n");
        return 1;
    }

    char input[2048];

    printf("You: ");
    fflush(stdout);

    if (!fgets(input, sizeof(input), stdin)) {
        fprintf(stderr, "no input\n");
        return 1;
    }

    input[strcspn(input, "\r\n")] = '\0';
    if (input[0] == '\0') return 0;

    msgs *convo = msgs_new();
    if (!convo) {
        fprintf(stderr, "out of memory\n");
        return 1;
    }

    msgs_add_user(convo, input);

    int finished = 0;   /* 是否正常结束（用来区分"答完了"和"被掐断了"） */

    for (int round = 0; round < MAX_ROUNDS; round++) {
        char *request = msgs_to_json(convo);
        if (!request) {
            fprintf(stderr, "out of memory\n");
            break;
        }

        llm_result *r = call_llm(request);
        free(request);                    /* 每一轮都要放 */

        if (!r) break;                    /* 出口三：请求失败 */

        if (r->call_count == 0) {         /* 出口一：模型不再要工具了 */
            printf("AI: %s\n", r->content ? r->content : "(empty)");
            llm_result_free(r);
            finished = 1;
            break;
        }

        /* 先把助手那条原文回填，再回填工具结果。
         *
         * 顺序不能反！带 tool_calls 的 assistant 消息必须**紧跟**在
         * 它对应的 tool 结果之前，这是 OpenAI 协议的硬要求。
         * 反了的话接口返回 400，而错误信息通常不会告诉你顺序错了。
         *
         * 注意这里依赖 r->assistant_json 非空。如果 llm.c 那边忘了在
         * cJSON_Delete 之前把它拿出来，历史就会缺一条 assistant，
         * 下一轮请求立刻被接口拒绝 —— 症状是"第二轮开始报 400"。 */
        msgs_add_assistant_raw(convo, r->assistant_json);

        printf("--- round %d: %d tool call(s) ---\n", round + 1, r->call_count);

        for (int i = 0; i < r->call_count; i++) {
            tool_call *c = &r->calls[i];

            printf("  -> %s(%s)\n",
                   c->name ? c->name : "(null)",
                   c->arguments ? c->arguments : "{}");

            char *out = tool_run(c->name, c->arguments);

            /* id 和 out 都兜底成空串：msgs_add_tool_result 遇到 NULL 会
             * 整条丢掉，那样历史里就会出现"assistant 说了要调工具、
             * 却没有对应结果"的残缺状态，接口同样会报错。 */
            msgs_add_tool_result(convo, c->id ? c->id : "", out ? out : "");

            if (out) {
                /* 只打印摘要，完整内容交给模型去看 */
                printf("     result: %.60s%s\n", out,
                       strlen(out) > 60 ? " ..." : "");
            }

            free(out);
        }

        llm_result_free(r);               /* 这条路径也要放 */
    }

    if (!finished) {
        printf("\nNote: stopped early (request failed, or MAX_ROUNDS=%d reached).\n",
               MAX_ROUNDS);
    }

    msgs_free(convo);
    return 0;
}
