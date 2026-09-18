#ifndef FILE_IO_H
#define FILE_IO_H

#include <stdio.h>
#include <stdlib.h>
#include <windows.h>

/* Only encoding conversion: paths are passed directly to the operating system. */
static inline wchar_t *file_path_wide(const char *path) {
    int n = MultiByteToWideChar(CP_UTF8, MB_ERR_INVALID_CHARS, path, -1, NULL, 0);
    wchar_t *wide = n ? malloc((size_t)n * sizeof(*wide)) : NULL;
    if (wide && !MultiByteToWideChar(CP_UTF8, MB_ERR_INVALID_CHARS, path, -1, wide, n)) {
        free(wide);
        return NULL;
    }
    return wide;
}

static inline FILE *file_open(const char *path, const wchar_t *mode) {
    wchar_t *wide = file_path_wide(path);
    FILE *fp = wide ? _wfopen(wide, mode) : NULL;
    free(wide);
    return fp;
}

#endif
