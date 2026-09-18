#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <direct.h>

/* 目标平台只有 Windows。_WIN32_WINNT 必须在 windows.h 之前定义。 */
#ifndef _WIN32_WINNT
#define _WIN32_WINNT 0x0600
#endif

#include <wchar.h>
#include <windows.h>

#include "tool_definition.h"
#include "strbuf.h"
#include "text_output.h"

#define OUTPUT_LIMIT 4000
/* 多留一个字符的空间，用于在输出截断时保持 UTF-8 完整。 */
typedef struct {
    unsigned char bytes[OUTPUT_LIMIT + 4];
    size_t len;
    int truncated;
    int timed_out;
    unsigned long exit_code;
} command_result;

static void collect(command_result *r, const char *data, size_t n) {
    size_t keep = sizeof(r->bytes) - r->len;
    if (keep > n) keep = n;
    memcpy(r->bytes + r->len, data, keep);
    r->len += keep;
    if (keep < n || r->len > OUTPUT_LIMIT) r->truncated = 1;
}

static wchar_t *to_wide(const char *text) {
    int n = MultiByteToWideChar(CP_UTF8, MB_ERR_INVALID_CHARS, text, -1, NULL, 0);
    wchar_t *w = n > 0 ? malloc((size_t)n * sizeof(*w)) : NULL;
    if (w && !MultiByteToWideChar(CP_UTF8, MB_ERR_INVALID_CHARS, text, -1, w, n)) {
        free(w); return NULL;
    }
    return w;
}

static int execute_command(const char *command, const wchar_t *cwd, unsigned timeout,
                           command_result *result) {
    HANDLE rd = NULL, wr = NULL, input = INVALID_HANDLE_VALUE, job = NULL;
    PROCESS_INFORMATION pi = {0};
    STARTUPINFOW si = {0};
    SECURITY_ATTRIBUTES sa = {sizeof(sa), NULL, TRUE};
    wchar_t shell[MAX_PATH], *line = NULL;
    strbuf cmd;
    sb_init(&cmd);
    int ok = 0;
    /* 使用系统目录内的 cmd.exe，避免当前目录中的同名程序劫持。 */
    UINT n = GetSystemDirectoryW(shell, MAX_PATH);
    if (!n || n + 9 >= MAX_PATH) goto done;
    wcscat(shell, L"\\cmd.exe");
    if (!sb_printf(&cmd, "cmd.exe /d /s /c \"\"%%SystemRoot%%\\System32\\chcp.com\" 65001 >nul && %s\"", command)) goto done;
    line = to_wide(cmd.data);
    if (!line || !CreatePipe(&rd, &wr, &sa, 0) ||
        !SetHandleInformation(rd, HANDLE_FLAG_INHERIT, 0)) goto done;
    input = CreateFileW(L"NUL", GENERIC_READ, FILE_SHARE_READ | FILE_SHARE_WRITE,
                        &sa, OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, NULL);
    if (input == INVALID_HANDLE_VALUE) goto done;
    job = CreateJobObjectW(NULL, NULL);
    JOBOBJECT_EXTENDED_LIMIT_INFORMATION limits = {0};
    limits.BasicLimitInformation.LimitFlags = JOB_OBJECT_LIMIT_KILL_ON_JOB_CLOSE;
    if (!job || !SetInformationJobObject(job, JobObjectExtendedLimitInformation,
                                         &limits, sizeof(limits))) goto done;
    si.cb = sizeof(si);
    si.dwFlags = STARTF_USESTDHANDLES | STARTF_USESHOWWINDOW;
    si.wShowWindow = SW_HIDE;
    si.hStdInput = input;
    si.hStdOutput = wr;
    si.hStdError = wr;
    if (!CreateProcessW(shell, line, NULL, NULL, TRUE,
                        CREATE_SUSPENDED | CREATE_NEW_CONSOLE, NULL, cwd, &si, &pi)) goto done;
    if (!AssignProcessToJobObject(job, pi.hProcess) || ResumeThread(pi.hThread) == (DWORD)-1) {
        TerminateProcess(pi.hProcess, 1);
        WaitForSingleObject(pi.hProcess, INFINITE);
        goto done;
    }
    CloseHandle(wr); wr = NULL;
    ULONGLONG start = GetTickCount64();
    for (;;) {
        DWORD available = 0;
        for (int i = 0; i < 16; i++) {
            if (!PeekNamedPipe(rd, NULL, 0, NULL, &available, NULL)) {
                if (GetLastError() != ERROR_BROKEN_PIPE) goto done;
                available = 0;
                break;
            }
            if (!available) break;
            char buffer[1024];
            DWORD got;
            if (!ReadFile(rd, buffer, available < sizeof(buffer) ? available : sizeof(buffer), &got, NULL)) goto done;
            collect(result, buffer, got);
        }
        DWORD state = WaitForSingleObject(pi.hProcess, 0);
        if (state == WAIT_FAILED) goto done;
        if (state == WAIT_OBJECT_0) {
            /* 先保存主命令退出码，再清理仍持有输出管道的后台子进程。 */
            DWORD code;
            if (!GetExitCodeProcess(pi.hProcess, &code)) goto done;
            result->exit_code = code;
            TerminateJobObject(job, 1);
            /* 主进程已退出，管道里剩余数据有界；继续读到空。 */
            while (PeekNamedPipe(rd, NULL, 0, NULL, &available, NULL) && available) {
                char buffer[1024]; DWORD got;
                if (!ReadFile(rd, buffer, available < sizeof(buffer) ? available : sizeof(buffer), &got, NULL)) break;
                collect(result, buffer, got);
            }
            ok = 1;
            break;
        }
        if (GetTickCount64() - start >= timeout) {
            result->timed_out = 1;
            result->exit_code = 124;
            TerminateJobObject(job, 124);
            WaitForSingleObject(pi.hProcess, INFINITE);
            ok = 1;
            break;
        }
        Sleep(10);
    }
done:
    if (job) CloseHandle(job);
    if (pi.hThread) CloseHandle(pi.hThread);
    if (pi.hProcess) CloseHandle(pi.hProcess);
    if (rd) CloseHandle(rd);
    if (wr) CloseHandle(wr);
    if (input != INVALID_HANDLE_VALUE) CloseHandle(input);
    free(line); sb_free(&cmd);
    return ok;
}

/* 命令可能输出任意字节。保留有效 UTF-8，其余字节以 ? 表示，避免破坏 JSON。 */
static void format_output(strbuf *out, const command_result *r) {
    size_t limit = r->len < OUTPUT_LIMIT ? r->len : OUTPUT_LIMIT;
    for (size_t i = 0; i < limit;) {
        unsigned char c = r->bytes[i];
        size_t n = tool_utf8_char_size(r->bytes + i, r->len - i);
        if (n && i + n > limit && r->truncated) break;
        int valid = n != 0;
        if (valid && c) { sb_write(out, (const char *)r->bytes + i, n); i += n; }
        else { sb_putc(out, '?'); i++; }
    }
    if (r->truncated) sb_puts(out, "\n... [truncated]");
    if (r->timed_out) sb_puts(out, "\nerror: command timed out");
    sb_printf(out, "\nexit_code: %lu", r->exit_code);
}

static char *run_command(const cJSON *args) {
    strbuf out;
    sb_init(&out);
    const cJSON *cmd = cJSON_GetObjectItemCaseSensitive(args, "command");
    const cJSON *timeout = cJSON_GetObjectItemCaseSensitive(args, "timeout_ms");
    if (!cJSON_IsString(cmd) || !cmd->valuestring[0]) {
        sb_puts(&out, "error: required non-empty string parameter \"command\"");
        return sb_detach(&out);
    }
    unsigned ms = 30000;
    if (timeout) {
        if (!cJSON_IsNumber(timeout) || !(timeout->valuedouble >= 1 && timeout->valuedouble <= 120000) ||
            timeout->valuedouble != (double)(unsigned)timeout->valuedouble) {
            sb_puts(&out, "error: timeout_ms must be an integer from 1 to 120000");
            return sb_detach(&out);
        }
        ms = (unsigned)timeout->valuedouble;
    }
    wchar_t *cwd = _wgetcwd(NULL, 0);
    if (cwd && cwd[0] == L'\\' && cwd[1] == L'\\') {
        free(cwd);
        sb_puts(&out, "error: cmd.exe requires a drive-letter workspace (UNC working directories are unsupported)");
        return sb_detach(&out);
    }
    command_result result = {0};
    if (!cwd || !execute_command(cmd->valuestring, cwd, ms, &result))
        sb_puts(&out, "error: command could not be executed");
    else format_output(&out, &result);
    free(cwd);
    return sb_detach(&out);
}

const tool_definition tool_run_command_definition = {
    .name = "run_command",
    .description = "在工作区启动 Windows cmd.exe 命令，返回标准输出、错误输出和退出码。默认超时 30000ms，输出最多约 4000 字节。命令拥有当前用户权限，不受文件工具路径限制。",
    .parameters_json = "{\"type\":\"object\",\"properties\":{\"command\":{\"type\":\"string\",\"description\":\"要执行的 cmd.exe 命令\"},\"timeout_ms\":{\"type\":\"integer\",\"minimum\":1,\"maximum\":120000,\"description\":\"超时毫秒数，默认 30000\"}},\"required\":[\"command\"]}",
    .execute = run_command
};
