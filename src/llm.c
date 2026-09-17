#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <windows.h>

#include "cJSON.h"
#include "client.h"
#include "llm.h"

static wchar_t *to_wide(const char *s) {
    if (!s) return NULL;

    int n = MultiByteToWideChar(CP_UTF8, 0, s, -1, NULL, 0);
    if (!n) return NULL;

    wchar_t *w = malloc(sizeof(wchar_t) * n);
    if (!w) return NULL;

    if (!MultiByteToWideChar(CP_UTF8, 0, s, -1, w, n)) {
        free(w);
        return NULL;
    }

    return w;
}

static wchar_t *make_auth(const char *key) {
    size_t n = strlen(key) + 32;
    char *buf = malloc(n);
    if (!buf) return NULL;

    snprintf(buf, n, "Authorization: Bearer %s\r\n", key);

    wchar_t *w = to_wide(buf);
    free(buf);

    return w;
}

static char *make_json(const char *model, const char *prompt) {
    cJSON *root = cJSON_CreateObject();
    cJSON *messages = cJSON_AddArrayToObject(root, "messages");
    cJSON *message = cJSON_CreateObject();

    cJSON_AddStringToObject(root, "model", model);
    cJSON_AddStringToObject(message, "role", "user");
    cJSON_AddStringToObject(message, "content", prompt);
    cJSON_AddItemToArray(messages, message);

    char *json = cJSON_PrintUnformatted(root);
    cJSON_Delete(root);

    return json;
}

static char *parse_answer(const char *response) {
    cJSON *root = cJSON_Parse(response);
    if (!root) return NULL;

    cJSON *choices = cJSON_GetObjectItem(root, "choices");
    cJSON *choice = cJSON_GetArrayItem(choices, 0);
    cJSON *message = cJSON_GetObjectItem(choice, "message");
    cJSON *content = cJSON_GetObjectItem(message, "content");

    if (!cJSON_IsString(content)) {
        cJSON_Delete(root);
        return NULL;
    }

    char *answer = malloc(strlen(content->valuestring) + 1);
    if (answer) strcpy(answer, content->valuestring);

    cJSON_Delete(root);
    return answer;
}

char *call_llm(const char *prompt) {
    const char *host_s = getenv("LLM_HOST");
    const char *path_s = getenv("LLM_PATH");
    const char *model = getenv("LLM_MODEL");
    const char *key = getenv("LLM_API_KEY");

    if (!host_s || !path_s || !model || !key) {
        fprintf(stderr, "LLM env missing\n");
        return NULL;
    }

    wchar_t *host = to_wide(host_s);
    wchar_t *path = to_wide(path_s);
    wchar_t *auth = make_auth(key);
    char *json = make_json(model, prompt);

    if (!host || !path || !auth || !json) {
        free(host);
        free(path);
        free(auth);
        cJSON_free(json);
        return NULL;
    }

    char *response = client_post_json(host, path, json, auth);

    free(host);
    free(path);
    free(auth);
    cJSON_free(json);

    if (!response) return NULL;

    char *answer = parse_answer(response);
    free(response);

    return answer;
}
