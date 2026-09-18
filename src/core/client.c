#include <windows.h>
#include <winhttp.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "client.h"

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

    session = WinHttpOpen(
        L"MiniCAgent/1.0",
        WINHTTP_ACCESS_TYPE_DEFAULT_PROXY,
        WINHTTP_NO_PROXY_NAME,
        WINHTTP_NO_PROXY_BYPASS,
        0
    );

    if (!session) goto cleanup;

    connect = WinHttpConnect(
        session,
        host,
        INTERNET_DEFAULT_HTTPS_PORT,
        0
    );

    if (!connect) goto cleanup;

    request = WinHttpOpenRequest(
        connect,
        L"POST",
        path,
        NULL,
        WINHTTP_NO_REFERER,
        WINHTTP_DEFAULT_ACCEPT_TYPES,
        WINHTTP_FLAG_SECURE
    );

    if (!request) goto cleanup;

    WinHttpAddRequestHeaders(
        request,
        L"Content-Type: application/json\r\n",
        -1L,
        WINHTTP_ADDREQ_FLAG_ADD
    );

    if (authorization) {
        WinHttpAddRequestHeaders(
            request,
            authorization,
            -1L,
            WINHTTP_ADDREQ_FLAG_ADD
        );
    }

    DWORD json_size = (DWORD)strlen(json);

    if (!WinHttpSendRequest(
        request,
        WINHTTP_NO_ADDITIONAL_HEADERS,
        0,
        (LPVOID)json,
        json_size,
        json_size,
        0
    )) {
        goto cleanup;
    }

    if (!WinHttpReceiveResponse(request, NULL)) {
        goto cleanup;
    }

    for (;;) {
        DWORD available = 0;

        if (!WinHttpQueryDataAvailable(
            request,
            &available
        )) {
            goto cleanup;
        }

        if (available == 0) break;

        char *new_response = realloc(
            response,
            response_size + available + 1
        );

        if (!new_response) {
            free(response);
            response = NULL;
            goto cleanup;
        }

        response = new_response;

        DWORD bytes_read = 0;

        if (!WinHttpReadData(
            request,
            response + response_size,
            available,
            &bytes_read
        )) {
            free(response);
            response = NULL;
            goto cleanup;
        }

        response_size += bytes_read;
        response[response_size] = '\0';
    }

cleanup:
    if (request) WinHttpCloseHandle(request);
    if (connect) WinHttpCloseHandle(connect);
    if (session) WinHttpCloseHandle(session);

    return response;
}
