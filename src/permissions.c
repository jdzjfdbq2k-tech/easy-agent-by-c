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

/* 在创建目标前锁住根目录及每级父目录，拒绝 reparse points。
 * 不共享 DELETE，防止检查与使用之间重命名目录或替换目录联接。 */
typedef struct {
    HANDLE *handles;
    size_t count;
    wchar_t full[PATH_CAP];
} locked_path;

static void unlock_path(locked_path *p) {
    while (p->count) CloseHandle(p->handles[--p->count]);
    free(p->handles);
    p->handles = NULL;
}

static int lock_directory(locked_path *p) {
    HANDLE h = CreateFileW(p->full, FILE_LIST_DIRECTORY,
        FILE_SHARE_READ | FILE_SHARE_WRITE, NULL, OPEN_EXISTING,
        FILE_FLAG_BACKUP_SEMANTICS | FILE_FLAG_OPEN_REPARSE_POINT, NULL);
    if (h == INVALID_HANDLE_VALUE) return 0;
    BY_HANDLE_FILE_INFORMATION info;
    if (!GetFileInformationByHandle(h, &info) ||
        !(info.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY) ||
        (info.dwFileAttributes & FILE_ATTRIBUTE_REPARSE_POINT)) {
        CloseHandle(h);
        return 0;
    }
    p->handles[p->count++] = h;
    return 1;
}

static int lock_path(const char *path, int directory, locked_path *p) {
    memset(p, 0, sizeof(*p));
    if (!*workspace) return 0;
    char *relative = relative_path(path);
    if (!relative) return 0;
    size_t capacity = strlen(relative) + 2;
    p->handles = calloc(capacity, sizeof(HANDLE));
    wchar_t wide[PATH_CAP];
    int ok = p->handles && MultiByteToWideChar(CP_UTF8, MB_ERR_INVALID_CHARS,
        relative, -1, wide, PATH_CAP);
    free(relative);
    if (!ok) { unlock_path(p); return 0; }
    wcscpy(p->full, workspace);
    /* 去掉尾分隔符，但保留卷根目录的反斜杠。 */
    size_t len = wcslen(p->full);
    if (len > 7) p->full[len - 1] = 0;
    if (!lock_directory(p)) goto fail;
    for (wchar_t *part = wide; *part;) {
        wchar_t *slash = wcschr(part, L'/');
        if (slash) *slash = 0;
        if (wcscmp(part, L".") != 0) {
            len = wcslen(p->full);
            size_t n = wcslen(part);
            if (len + n + 2 >= PATH_CAP) goto fail;
            if (p->full[len - 1] != L'\\') p->full[len++] = L'\\';
            wcscpy(p->full + len, part);
            if ((slash || directory) && !lock_directory(p)) goto fail;
        } else if (!slash && !directory) goto fail;
        if (!slash) break;
        part = slash + 1;
    }
    return 1;
fail:
    unlock_path(p);
    return 0;
}

FILE *permissions_open_write(const char *path) {
    locked_path p;
    if (!lock_path(path, 0, &p)) return NULL;
    HANDLE h = CreateFileW(p.full, GENERIC_WRITE, 0, NULL, OPEN_ALWAYS,
                          FILE_ATTRIBUTE_NORMAL | FILE_FLAG_OPEN_REPARSE_POINT, NULL);
    unlock_path(&p);
    if (h == INVALID_HANDLE_VALUE) return NULL;
    BY_HANDLE_FILE_INFORMATION info;
    if (GetFileType(h) != FILE_TYPE_DISK || !GetFileInformationByHandle(h, &info) ||
        (info.dwFileAttributes & (FILE_ATTRIBUTE_DIRECTORY | FILE_ATTRIBUTE_REPARSE_POINT)) ||
        info.nNumberOfLinks != 1 || !SetEndOfFile(h)) {
        CloseHandle(h);
        return NULL;
    }
    int fd = _open_osfhandle((intptr_t)h, _O_WRONLY | _O_BINARY);
    if (fd < 0) { CloseHandle(h); return NULL; }
    FILE *fp = _fdopen(fd, "wb");
    if (!fp) _close(fd);
    return fp;
}

int permissions_list_dir(const char *path, permissions_dir_visitor visit, void *context) {
    locked_path p;
    if (!visit || !lock_path(path, 1, &p)) return 0;
    size_t len = wcslen(p.full);
    if (len + 3 >= PATH_CAP) { unlock_path(&p); return 0; }
    wcscat(p.full, L"\\*");
    WIN32_FIND_DATAW data;
    HANDLE find = FindFirstFileW(p.full, &data);
    int ok = 1;
    if (find == INVALID_HANDLE_VALUE) {
        ok = GetLastError() == ERROR_FILE_NOT_FOUND;
    } else {
        for (;;) {
            if (wcscmp(data.cFileName, L".") && wcscmp(data.cFileName, L"..")) {
                char name[4 * MAX_PATH];
                if (!WideCharToMultiByte(CP_UTF8, WC_ERR_INVALID_CHARS, data.cFileName,
                                        -1, name, sizeof(name), NULL, NULL)) { ok = 0; break; }
                int is_dir = (data.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY) &&
                             !(data.dwFileAttributes & FILE_ATTRIBUTE_REPARSE_POINT);
                if (!visit(name, is_dir, context)) break;
            }
            if (!FindNextFileW(find, &data)) {
                ok = GetLastError() == ERROR_NO_MORE_FILES;
                break;
            }
        }
        FindClose(find);
    }
    unlock_path(&p);
    return ok;
}

char *permissions_workspace_path(void) {
    if (!*workspace) return NULL;
    /* cmd.exe 不接受扩展路径前缀，转换回普通 DOS/UNC 路径。 */
    wchar_t normal[PATH_CAP];
    const wchar_t *path = workspace;
    if (!wcsncmp(workspace, L"\\\\?\\UNC\\", 8)) {
        wcscpy(normal, L"\\\\");
        wcscat(normal, workspace + 8);
        path = normal;
    } else if (!wcsncmp(workspace, L"\\\\?\\", 4)) path = workspace + 4;
    int n = WideCharToMultiByte(CP_UTF8, 0, path, -1, NULL, 0, NULL, NULL);
    char *result = n > 0 ? malloc((size_t)n) : NULL;
    if (result && !WideCharToMultiByte(CP_UTF8, 0, path, -1, result, n, NULL, NULL)) {
        free(result);
        return NULL;
    }
    return result;
}

#else
#include <fcntl.h>
#include <sys/stat.h>
#include <unistd.h>
#include <dirent.h>
#include <errno.h>

static int workspace = -1;
static char *workspace_path;

int permissions_init(void) {
    if (workspace >= 0) return 1;
    workspace_path = getcwd(NULL, 0);
    if (!workspace_path) return 0;
    workspace = open(".", O_RDONLY | O_DIRECTORY | O_CLOEXEC);
    if (workspace < 0) { free(workspace_path); workspace_path = NULL; }
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
/* 从固定工作区目录句柄逐级打开；所有路径段都拒绝符号链接。 */
static int open_relative(const char *path, int writing) {
    if (workspace < 0) return -1;
    char *relative = relative_path(path);
    if (!relative) return -1;
    size_t len = strlen(relative);
    if (!writing && len > 1 && relative[len - 1] == '/') relative[len - 1] = 0;
    int fd = dup(workspace);
    for (char *p = relative; fd >= 0;) {
        char *slash = strchr(p, '/');
        if (slash) *slash = 0;
        int flags = O_NOFOLLOW | O_CLOEXEC | O_NONBLOCK;
        flags |= slash || !writing ? O_RDONLY | O_DIRECTORY : O_WRONLY | O_CREAT;
        int next = openat(fd, p, flags, 0666);
        close(fd);
        fd = next;
        if (!slash) break;
        p = slash + 1;
    }
    free(relative);
    return fd;
}

FILE *permissions_open_write(const char *path) {
    int fd = open_relative(path, 1);
    if (fd < 0) return NULL;
    struct stat st;
    /* 先验证实际文件，再截断，避免破坏硬链接对应的区外文件。 */
    if (fstat(fd, &st) || !S_ISREG(st.st_mode) || st.st_nlink != 1 || ftruncate(fd, 0)) {
        close(fd);
        return NULL;
    }
    FILE *fp = fdopen(fd, "wb");
    if (!fp) close(fd);
    return fp;
}

int permissions_list_dir(const char *path, permissions_dir_visitor visit, void *context) {
    if (!visit) return 0;
    int fd = open_relative(path, 0);
    if (fd < 0) return 0;
    DIR *dir = fdopendir(fd);
    if (!dir) { close(fd); return 0; }
    int ok = 1;
    for (;;) {
        errno = 0;
        struct dirent *e = readdir(dir);
        if (!e) { ok = errno == 0; break; }
        if (!strcmp(e->d_name, ".") || !strcmp(e->d_name, "..")) continue;
        struct stat st;
        if (fstatat(fd, e->d_name, &st, AT_SYMLINK_NOFOLLOW)) { ok = 0; break; }
        if (!visit(e->d_name, S_ISDIR(st.st_mode), context)) break;
    }
    closedir(dir);
    return ok;
}

char *permissions_workspace_path(void) {
    return workspace_path ? strdup(workspace_path) : NULL;
}

int permissions_enter_workspace(void) {
    return workspace >= 0 && fchdir(workspace) == 0;
}
#endif
