#include <windows.h>
#include <winhttp.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "client.h"

static const char *error_hint(DWORD error) {
    switch (error) {
        case ERROR_WINHTTP_TIMEOUT: return "request timed out";
        case ERROR_WINHTTP_NAME_NOT_RESOLVED: return "DNS lookup failed";
        case ERROR_WINHTTP_CANNOT_CONNECT: return "could not connect to server or proxy";
        case ERROR_WINHTTP_CONNECTION_ERROR: return "connection interrupted or protocol error";
        case ERROR_WINHTTP_SECURE_FAILURE: return "TLS certificate or secure connection failed";
        case ERROR_NOT_ENOUGH_MEMORY: return "out of memory";
        default: return "see Windows error code";
    }
}

char *client_post_json(
    const wchar_t *host,
    const wchar_t *path,
    const char *json,
    const wchar_t *authorization
) {
    HINTERNET session = NULL;
    HINTERNET connect = NULL;
    HINTERNET request = NULL;

    char *response = NULL;
    DWORD response_size = 0;
    DWORD status = 0;
    ULONGLONG started = GetTickCount64();
    const char *stage = "WinHttpOpen";

    session = WinHttpOpen(
        L"MiniCAgent/1.0",
        WINHTTP_ACCESS_TYPE_DEFAULT_PROXY,
        WINHTTP_NO_PROXY_NAME,
        WINHTTP_NO_PROXY_BYPASS,
        0
    );

    if (!session) goto failed;

    stage = "WinHttpConnect";
    connect = WinHttpConnect(
        session,
        host,
        INTERNET_DEFAULT_HTTPS_PORT,
        0
    );

    if (!connect) goto failed;

    stage = "WinHttpOpenRequest";
    request = WinHttpOpenRequest(
        connect,
        L"POST",
        path,
        NULL,
        WINHTTP_NO_REFERER,
        WINHTTP_DEFAULT_ACCEPT_TYPES,
        WINHTTP_FLAG_SECURE
    );

    if (!request) goto failed;

    /* 非流式生成可能较慢，接收数据最多等待 180 秒。 */
    DWORD receive_timeout = 180000;
    stage = "WinHttpSetOption(receive timeout)";
    if (!WinHttpSetOption(request, WINHTTP_OPTION_RECEIVE_TIMEOUT,
                          &receive_timeout, sizeof(receive_timeout))) goto failed;

    stage = "WinHttpAddRequestHeaders";
    if (!WinHttpAddRequestHeaders(
        request,
        L"Content-Type: application/json\r\n",
        -1L,
        WINHTTP_ADDREQ_FLAG_ADD
    )) goto failed;

    if (authorization) {
        if (!WinHttpAddRequestHeaders(
            request,
            authorization,
            -1L,
            WINHTTP_ADDREQ_FLAG_ADD
        )) goto failed;
    }

    DWORD json_size = (DWORD)strlen(json);

    stage = "WinHttpSendRequest";
    if (!WinHttpSendRequest(
        request,
        WINHTTP_NO_ADDITIONAL_HEADERS,
        0,
        (LPVOID)json,
        json_size,
        json_size,
        0
    )) {
        goto failed;
    }

    stage = "WinHttpReceiveResponse";
    if (!WinHttpReceiveResponse(request, NULL)) {
        goto failed;
    }

    stage = "WinHttpQueryHeaders";
    DWORD status_size = sizeof(status);
    if (!WinHttpQueryHeaders(request, WINHTTP_QUERY_STATUS_CODE | WINHTTP_QUERY_FLAG_NUMBER,
                             WINHTTP_HEADER_NAME_BY_INDEX, &status, &status_size,
                             WINHTTP_NO_HEADER_INDEX)) goto failed;
    if (status < 200 || status >= 300) {
        fprintf(stderr, "[http] status=%lu elapsed=%.2fs\n", status,
                (GetTickCount64() - started) / 1000.0);
    }

    for (;;) {
        DWORD available = 0;

        stage = "WinHttpQueryDataAvailable";
        if (!WinHttpQueryDataAvailable(
            request,
            &available
        )) {
            goto failed;
        }

        if (available == 0) break;

        char *new_response = realloc(
            response,
            response_size + available + 1
        );

        if (!new_response) {
            stage = "response allocation";
            SetLastError(ERROR_NOT_ENOUGH_MEMORY);
            goto failed;
        }

        response = new_response;

        DWORD bytes_read = 0;

        stage = "WinHttpReadData";
        if (!WinHttpReadData(
            request,
            response + response_size,
            available,
            &bytes_read
        )) {
            goto failed;
        }

        response_size += bytes_read;
        response[response_size] = '\0';
    }

    if (!response_size) {
        fprintf(stderr, "[http] empty response body: status=%lu elapsed=%.2fs\n",
                status, (GetTickCount64() - started) / 1000.0);
        free(response);
        response = NULL;
    }
    goto cleanup;

failed: {
    /* Capture before formatting or closing handles can replace the error code. */
    DWORD error = GetLastError();
    char message[512] = {0};
    FormatMessageA(FORMAT_MESSAGE_FROM_SYSTEM | FORMAT_MESSAGE_FROM_HMODULE |
                   FORMAT_MESSAGE_IGNORE_INSERTS, GetModuleHandleW(L"winhttp.dll"), error,
                   MAKELANGID(LANG_ENGLISH, SUBLANG_ENGLISH_US), message, sizeof(message), NULL);
    message[strcspn(message, "\r\n")] = '\0';
    fprintf(stderr, "[http] stage=%s error=%lu (%s) status=%lu elapsed=%.2fs\n",
            stage, error, message[0] ? message : error_hint(error), status,
            (GetTickCount64() - started) / 1000.0);
    free(response);
    response = NULL;
}

cleanup:
    if (request) WinHttpCloseHandle(request);
    if (connect) WinHttpCloseHandle(connect);
    if (session) WinHttpCloseHandle(session);

    return response;
}
