#ifndef CLIENT_H
#define CLIENT_H

#include <wchar.h>

char *client_post_json(
    const wchar_t *host,
    const wchar_t *path,
    const char *json,
    const wchar_t *authorization
);

#endif
