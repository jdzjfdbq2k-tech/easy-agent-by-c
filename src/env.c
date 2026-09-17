#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "env.h"

int load_env(const char *filename) {
    FILE *fp = fopen(filename, "r");
    if (!fp) return 0;

    char line[2048];

    while (fgets(line, sizeof(line), fp)) {
        char *p = line;

        while (*p == ' ' || *p == '\t') p++;
        if (*p == '#' || *p == '\n' || *p == '\0') continue;

        p[strcspn(p, "\r\n")] = '\0';

        char *eq = strchr(p, '=');
        if (!eq) continue;

        *eq = '\0';

        char *key = p;
        char *value = eq + 1;

        while (*value == ' ' || *value == '\t') value++;

        if ((*value == '"' || *value == '\'') && strlen(value) >= 2) {
            char quote = *value++;
            char *end = strrchr(value, quote);
            if (end) *end = '\0';
        }

        _putenv_s(key, value);
    }

    fclose(fp);
    return 1;
}
