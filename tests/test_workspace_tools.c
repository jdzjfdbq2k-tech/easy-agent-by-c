#define _POSIX_C_SOURCE 200809L
#ifdef __APPLE__
#define _DARWIN_C_SOURCE
#endif
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <time.h>
#include <unistd.h>
#include <fcntl.h>
#include "tools.h"
#include "permissions.h"

#define CHECK(x) do { if (!(x)) { fprintf(stderr, "FAIL line %d: %s\n", __LINE__, #x); return 1; } } while (0)
static int error(const char *name, const char *args) {
    char *out = tool_run(name, args);
    int denied = out && !strncmp(out, "error:", 6) && !strstr(out, "unknown tool");
    free(out);
    return denied;
}
static double seconds(void) {
    struct timespec now;
    clock_gettime(CLOCK_MONOTONIC, &now);
    return now.tv_sec + now.tv_nsec / 1e9;
}
int main(void) {
    CHECK(error("list_dir", "{\"path\":\".\"}"));
    CHECK(error("write_file", "{\"path\":\"new\",\"content\":\"x\"}"));
    CHECK(error("run_command", "{\"command\":\"echo x\"}"));
    char fixture[] = "/tmp/easy-agent-tools-XXXXXX";
    CHECK(mkdtemp(fixture));
    int saved = open(".", O_RDONLY);
    CHECK(saved >= 0 && chdir(fixture) == 0 && mkdir("workspace", 0700) == 0);
    FILE *fp = fopen("secret", "wb");
    CHECK(fp && fputs("outside", fp) >= 0 && fclose(fp) == 0);
    CHECK(chdir("workspace") == 0);
    CHECK(symlink("../secret", "link") == 0);
    CHECK(symlink("..", "escape") == 0);
    CHECK(link("../secret", "hardlink") == 0);
    CHECK(mkfifo("fifo", 0600) == 0);
    CHECK(mkdir("empty", 0700) == 0);
    CHECK(permissions_init());
    CHECK(error("write_file", "{\"path\":\"link\",\"content\":\"changed\"}"));
    CHECK(error("write_file", "{\"path\":\"hardlink\",\"content\":\"changed\"}"));
    CHECK(error("write_file", "{\"path\":\"escape/new\",\"content\":\"changed\"}"));
    CHECK(error("write_file", "{\"path\":\"fifo\",\"content\":\"changed\"}"));
    CHECK(error("write_file", "{\"path\":\"empty\",\"content\":\"changed\"}"));
    CHECK(error("write_file", "{\"path\":\"missing/file\",\"content\":\"changed\"}"));
    CHECK(error("list_dir", "{\"path\":\"escape\"}"));
    CHECK(error("list_dir", "{\"path\":\"link\"}"));
    char *out = tool_run("list_dir", "{\"path\":\"empty\"}");
    CHECK(out && !strcmp(out, "(empty directory)")); free(out);
    CHECK(access("../new", F_OK) != 0);
    fp = fopen("../secret", "rb");
    char buffer[32] = {0};
    CHECK(fp && fread(buffer, 1, sizeof(buffer), fp) == 7 && !strcmp(buffer, "outside"));
    fclose(fp);
    out = tool_run("write_file", "{\"path\":\"marker\",\"content\":\"fixed-workspace\"}");
    CHECK(out && !strstr(out, "error:")); free(out);
    CHECK(chdir("..") == 0 && permissions_init());
    out = tool_run("run_command", "{\"command\":\"cat marker\"}");
    CHECK(out && strstr(out, "fixed-workspace") && strstr(out, "exit_code: 0")); free(out);
    out = tool_run("write_file", "{\"path\":\"marker\",\"content\":\"updated\"}");
    CHECK(out && !strstr(out, "error:") && access("marker", F_OK) != 0); free(out);
    out = tool_run("list_dir", "{\"path\":\".\"}");
    CHECK(out && strstr(out, "marker") && !strstr(out, "secret")); free(out);
    CHECK(rename("workspace", "moved") == 0 && mkdir("workspace", 0700) == 0);
    out = tool_run("run_command", "{\"command\":\"cat marker\"}");
    CHECK(out && strstr(out, "updated") && strstr(out, "exit_code: 0")); free(out);
    CHECK(rmdir("workspace") == 0 && rename("moved", "workspace") == 0);
    double start = seconds();
    out = tool_run("run_command", "{\"command\":\"sleep 2; echo late > late\",\"timeout_ms\":50}");
    CHECK(out && strstr(out, "timed out") && seconds() - start < 1.5); free(out);
    /* 后台进程在主 shell 退出后也不能留下延迟写入。 */
    out = tool_run("run_command", "{\"command\":\"(sleep 1; echo late > background) & echo done\"}");
    CHECK(out && strstr(out, "done")); free(out);
    struct timespec pause = {2, 100000000}; nanosleep(&pause, NULL);
    CHECK(access("workspace/late", F_OK) != 0 && access("workspace/background", F_OK) != 0);
    CHECK(chdir("workspace") == 0);
    CHECK(unlink("marker") == 0 && unlink("hardlink") == 0 && unlink("link") == 0);
    CHECK(unlink("escape") == 0 && unlink("fifo") == 0 && rmdir("empty") == 0);
    CHECK(chdir("..") == 0 && unlink("secret") == 0 && rmdir("workspace") == 0);
    CHECK(fchdir(saved) == 0); close(saved);
    CHECK(rmdir(fixture) == 0);
    puts("WORKSPACE TOOLS ALL PASS (links, overwrite safety, fixed root, process cleanup)");
    return 0;
}
