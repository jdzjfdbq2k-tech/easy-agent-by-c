#define _POSIX_C_SOURCE 200809L
#ifdef __APPLE__
#define _DARWIN_C_SOURCE
#endif
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <unistd.h>
#include <fcntl.h>

#include "permissions.h"

#define CHECK(condition) do { \
    if (!(condition)) { \
        fprintf(stderr, "FAIL at line %d: %s\n", __LINE__, #condition); \
        return 1; \
    } \
} while (0)

static int readable(const char *path, const char *expected) {
    FILE *fp = permissions_open_read(path);
    if (!fp) return 0;
    char buf[32] = {0};
    size_t n = fread(buf, 1, sizeof(buf) - 1, fp);
    fclose(fp);
    return n == strlen(expected) && strcmp(buf, expected) == 0;
}

static int denied(const char *path) {
    FILE *fp = permissions_open_read(path);
    if (!fp) return 1;
    fclose(fp);
    return 0;
}

int main(void) {
    CHECK(denied("src/main.c")); /* 未初始化不能读。 */
    char fixture[] = "/tmp/easy-agent-permissions-XXXXXX";
    CHECK(mkdtemp(fixture));
    int saved = open(".", O_RDONLY);
    CHECK(saved >= 0 && chdir(fixture) == 0);
    CHECK(mkdir("workspace", 0700) == 0);
    CHECK(mkdir("workspace-other", 0700) == 0);
    FILE *fp = fopen("workspace-other/secret.txt", "wb");
    CHECK(fp && fputs("outside", fp) >= 0);
    fclose(fp);
    CHECK(chdir("workspace") == 0);
    CHECK(mkdir("nested", 0700) == 0);
    fp = fopen("nested/file.txt", "wb");
    CHECK(fp && fputs("inside", fp) >= 0);
    fclose(fp);
    fp = fopen("nested/中文.txt", "wb");
    CHECK(fp && fputs("utf8", fp) >= 0);
    fclose(fp);
    CHECK(symlink("../../workspace-other/secret.txt", "nested/link.txt") == 0);
    CHECK(symlink("../workspace-other", "escape") == 0);
    CHECK(symlink("nested/file.txt", "inside-link") == 0);
    CHECK(mkfifo("pipe", 0600) == 0);
    CHECK(permissions_init());
    CHECK(readable("nested/file.txt", "inside"));
    CHECK(readable("./nested/file.txt", "inside"));
    CHECK(readable("nested\\file.txt", "inside"));
    CHECK(readable("nested/中文.txt", "utf8"));

    const char *bad[] = {NULL, "", "../workspace-other/secret.txt",
        "..\\workspace-other\\secret.txt", "nested/../../workspace-other/secret.txt",
        "/etc/passwd", "C:\\Windows\\win.ini", "C:secret.txt", "\\\\server\\share\\file",
        "\\\\?\\C:\\secret.txt", "nested/file.txt:stream", ".. /secret.txt",
        "nested/link.txt", "escape/secret.txt", "inside-link", "nested", "pipe", "missing"};
    for (size_t i = 0; i < sizeof(bad) / sizeof(bad[0]); i++) CHECK(denied(bad[i]));
    char absolute[512];
    snprintf(absolute, sizeof(absolute), "%s/workspace/nested/file.txt", fixture);
    CHECK(denied(absolute)); /* 即使指向工作区，接口也仅接受相对路径。 */

    CHECK(chdir("../workspace-other") == 0);
    CHECK(permissions_init()); /* 重复初始化不能扩大或改变授权范围。 */
    CHECK(readable("nested/file.txt", "inside"));
    CHECK(denied("secret.txt"));

    CHECK(chdir("../workspace") == 0);
    CHECK(unlink("nested/link.txt") == 0);
    CHECK(unlink("escape") == 0);
    CHECK(unlink("inside-link") == 0);
    CHECK(unlink("pipe") == 0);
    CHECK(unlink("nested/file.txt") == 0);
    CHECK(unlink("nested/中文.txt") == 0);
    CHECK(rmdir("nested") == 0);
    CHECK(chdir("..") == 0);
    CHECK(unlink("workspace-other/secret.txt") == 0);
    CHECK(rmdir("workspace-other") == 0);
    CHECK(rmdir("workspace") == 0);
    CHECK(fchdir(saved) == 0);
    close(saved);
    CHECK(rmdir(fixture) == 0);
    puts("PERMISSIONS ALL PASS (workspace, traversal, links, special files, fixed root)");
    return 0;
}
