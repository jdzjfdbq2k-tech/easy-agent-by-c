#ifndef _WIN32
#define _POSIX_C_SOURCE 200809L
#ifdef __APPLE__
#define _DARWIN_C_SOURCE
#endif
#endif

#include <stdlib.h>
#include <string.h>
#include "permissions.h"

/* 两个平台统一接受 / 和反斜杠；拒绝盘符、设备路径及父目录跳转。 */
static char *relative_path(const char *path) {
    if (!path || !*path || *path == '/' || *path == '\\' || strchr(path, ':'))
        return NULL;
    char *copy = malloc(strlen(path) + 1);
    if (!copy) return NULL;
    strcpy(copy, path);
    for (char *p = copy; *p; p++) if (*p == '\\') *p = '/';
    for (char *p = copy; *p;) {
        char *end = strchr(p, '/');
        size_t n = end ? (size_t)(end - p) : strlen(p);
        if (!n || (n == 2 && p[0] == '.' && p[1] == '.') ||
            (n > 1 && p[n - 1] == '.') || p[n - 1] == ' ') {
            free(copy);
            return NULL;
        }
        p += n;
        if (*p) p++;
    }
    return copy;
}

#ifdef _WIN32
#ifndef _WIN32_WINNT
#define _WIN32_WINNT 0x0600
#endif
#include <windows.h>
#include <io.h>
#include <fcntl.h>
#include <stdint.h>
#include <wchar.h>

#define PATH_CAP 32768
static wchar_t workspace[PATH_CAP];

int permissions_init(void) {
    if (*workspace) return 1;
    HANDLE dir = CreateFileW(L".", 0, FILE_SHARE_READ | FILE_SHARE_WRITE,
                            NULL, OPEN_EXISTING, FILE_FLAG_BACKUP_SEMANTICS, NULL);
    if (dir == INVALID_HANDLE_VALUE) return 0;
    DWORD n = GetFinalPathNameByHandleW(dir, workspace, PATH_CAP, 0);
    CloseHandle(dir);
    if (!n || n >= PATH_CAP - 1) {
        *workspace = 0;
        return 0;
    }
    if (workspace[n - 1] != L'\\') workspace[n++] = L'\\';
    workspace[n] = 0; /* 保留分隔符，避免将 workspace-other 当作子目录。 */
    return 1;
}

FILE *permissions_open_read(const char *path) {
    if (!*workspace) return NULL;
    char *relative = relative_path(path);
    if (!relative) return NULL;
    wchar_t full[PATH_CAP], final[PATH_CAP];
    size_t root_len = wcslen(workspace);
    wcscpy(full, workspace);
    int ok = MultiByteToWideChar(CP_UTF8, MB_ERR_INVALID_CHARS, relative, -1,
                                full + root_len, (int)(PATH_CAP - root_len));
    free(relative);
    if (!ok) return NULL;
    for (wchar_t *p = full + root_len; *p; p++) if (*p == L'/') *p = L'\\';

    HANDLE file = CreateFileW(full, GENERIC_READ, FILE_SHARE_READ,
                             NULL, OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, NULL);
    if (file == INVALID_HANDLE_VALUE) return NULL;
    BY_HANDLE_FILE_INFORMATION info;
    DWORD n = GetFinalPathNameByHandleW(file, final, PATH_CAP, 0);
    /* 验证实际打开的对象，再把同一句柄交给调用方，绝不按路径重新打开。
     * 大小写精确比较，遇到不一致保守拒绝，避免大小写敏感目录的同名越界。
     */
    if (!n || n >= PATH_CAP || n <= root_len ||
        wcsncmp(final, workspace, root_len) != 0 ||
        GetFileType(file) != FILE_TYPE_DISK ||
        !GetFileInformationByHandle(file, &info) ||
        (info.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY)) {
        CloseHandle(file);
        return NULL;
    }
    int fd = _open_osfhandle((intptr_t)file, _O_RDONLY | _O_BINARY);
    if (fd == -1) { CloseHandle(file); return NULL; }
    FILE *fp = _fdopen(fd, "rb");
    if (!fp) _close(fd);
    return fp;
}

#else
#include <fcntl.h>
#include <sys/stat.h>
#include <unistd.h>

static int workspace = -1;

int permissions_init(void) {
    if (workspace >= 0) return 1;
    workspace = open(".", O_RDONLY | O_DIRECTORY | O_CLOEXEC);
    return workspace >= 0;
}

FILE *permissions_open_read(const char *path) {
    if (workspace < 0) return NULL;
    char *relative = relative_path(path);
    if (!relative) return NULL;
    int fd = dup(workspace);
    /* 逐级从目录句柄打开，拒绝所有符号链接，不受后续 chdir 影响。 */
    for (char *p = relative; fd >= 0;) {
        char *slash = strchr(p, '/');
        if (slash) *slash = 0;
        int flags = O_RDONLY | O_NOFOLLOW | O_CLOEXEC | O_NONBLOCK;
        if (slash) flags |= O_DIRECTORY;
        int next = openat(fd, p, flags);
        close(fd);
        fd = next;
        if (!slash) break;
        p = slash + 1;
    }
    free(relative);
    if (fd < 0) return NULL;
    struct stat st;
    if (fstat(fd, &st) != 0 || !S_ISREG(st.st_mode)) {
        close(fd);
        return NULL;
    }
    FILE *fp = fdopen(fd, "rb");
    if (!fp) close(fd);
    return fp;
}
#endif
