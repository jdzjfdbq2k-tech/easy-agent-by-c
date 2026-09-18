# easy-agent-by-c

一个用 C 编写的 Windows 命令行 Agent。模型通过兼容 Chat Completions 的接口请求工具，程序执行工具后将结果回填到对话，最多调用模型 20 轮。目前内置 `read_file`、`list_dir`、`write_file` 和 `run_command`。

## 目录

```text
include/                 对外头文件，保持平铺
src/
  main.c                 用户输入和 Agent 循环
  llm.c                  模型请求与响应解析
  client.c               WinHTTP 网络请求
  env.c                  .env 加载
  permissions.c          工作区文件权限，供工具复用
  msgs.c                 对话历史
  strbuf.c               动态字符串
  cJSON.c                JSON 库
  tools/
    tool_definition.h    工具作者接口（内部使用）
    builtins.c           内置工具注册列表
    registry.c           schema 生成、参数解析和执行分发
    read_file.c          文件读取及其工具定义
    list_dir.c           列出目录
    write_file.c         创建或覆盖文本文件
    run_command.c        命令执行、超时与输出收集
    text_output.h        UTF-8 输出校验
tests/                  macOS/Linux 宿主测试
build/                  生成的程序、对象文件与测试程序（Git 忽略）
```

## 构建与运行

主程序依赖 Windows WinHTTP。Windows 可使用 MSYS2 MinGW；macOS/Linux 安装 mingw-w64 后可以交叉编译，但生成的程序仍须在 Windows 上运行。

```sh
make                         # 生成 build/agent.exe
make test                    # macOS/Linux 宿主测试，无需 MinGW
make clean                   # 清理本构建配置产生的文件
```

复制 `.env.example` 为 `.env`，填写 `LLM_HOST`、`LLM_PATH`、`LLM_MODEL`、`LLM_API_KEY`。在 Windows 的项目根目录启动 `build/agent.exe`，确保能找到 `.env`，文件工具的相对路径也以启动目录为基准。

根目录原有的 `agent.exe` 是旧产物；新构建结果位于 `build/agent.exe`。

## 添加工具

注册方式是编译期静态列表，无需运行时初始化，也不需要修改 `main.c` 或 `llm.c`。

1. 在 `src/tools/` 新建一个 `.c` 文件，实现工具并导出 `const tool_definition`。可参考 `read_file.c`：定义名称、描述、参数 JSON Schema 和执行函数。
2. 在 `builtins.c` 声明该定义，将其地址加入 `builtin_tools`。
3. 为新工具补充行为测试，执行 `make test`。构建配置自动收集 `src/tools/*.c`。

注册表会从同一份定义生成模型的 `tools` 数组，并按名称查找执行函数。重复名称、缺少必要元数据或执行函数会被拒绝；生成 schema 时还会检查参数定义为 `type: object`。

执行器接收已解析的 JSON 对象，自己校验字段类型及语义；不得释放或保存传入的对象。返回文本由 `malloc` 分配，调用方负责 `free`。一般错误应返回以 `error: ` 开头的文本，内存不足可返回 `NULL`。

公共接口 `tools_schema_json()` 现在返回动态生成的字符串，调用方必须 `free`，失败返回 `NULL`。`tool_run()` 拒绝非对象参数以及 JSON 后的多余内容；空字符串或 NULL 参数按 `{}` 处理。这里只检查 JSON 对象形态，不是完整的 JSON Schema 校验器。

## 工作区权限

程序启动时调用 `permissions_init()`，将启动目录固定为工作区；之后改变当前目录或重复初始化都不会改变授权根目录。权限实现集中在 `src/permissions.c`，声明位于 `include/permissions.h`。

文件读取工具统一使用 `permissions_open_read(path)`，成功返回 `FILE *`，使用后 `fclose`；未初始化、路径不允许或无法打开时返回 `NULL`。`read_file` 已接入。

只接受工作区相对路径，拒绝绝对路径、盘符、UNC/设备路径、任何 `..` 路径段及非普通文件。Windows 验证实际打开句柄的最终路径，阻止符号链接或目录联接指向工作区外；Mac/Linux 使用目录描述符逐级打开，保守拒绝所有符号链接，包括指向工作区内部的链接。路径末尾的点或空格也会被拒绝（单独的 `.` 路径段除外），避免 Windows 路径别名。

`list_dir` 通过 `permissions_list_dir()` 枚举目录，`write_file` 通过 `permissions_open_write()` 创建或覆盖文件。写入和枚举保守拒绝路径中的符号链接与 Windows reparse points；写入额外拒绝多重硬链接，并在验证实际文件后才截断旧内容。父目录必须存在，写入不是原子替换：磁盘错误可能留下部分内容。

本模块是文件工具的访问约束，不是操作系统沙箱。`run_command` 拥有当前用户权限，可以访问工作区外文件、联网或执行其他程序，不能用它来保证文件工具的路径限制。

## 内置工具参数

| 工具 | 参数示例 | 行为 |
| --- | --- | --- |
| `read_file` | `{"path":"src/main.c"}` | 读取文件，正文最多约 4000 字节 |
| `list_dir` | `{"path":"."}` | 列出直接子项，目录以 `/` 结尾；不递归、不保证排序 |
| `write_file` | `{"path":"notes.txt","content":"hello\n"}` | 创建或覆盖完整内容，空字符串可清空文件 |
| `run_command` | `{"command":"echo hello","timeout_ms":30000}` | 执行命令，合并 stdout/stderr，附上 `exit_code` |

目录列表与命令输出的正文上限为 4000 字节，超过时附上截断标记。目录名中的控制字节、反斜杠和非法 UTF-8 字节显示为 `\xHH`；命令输出中的非法 UTF-8 或 NUL 字节以 `?` 替换。合法中文不会在截断点被切成半个字符。

命令的 `timeout_ms` 可省略，默认 30000，允许 1–120000 的整数。超时返回 `error: command timed out` 和退出码 124。标准输入接到空设备，不支持交互输入。每次调用从初始化工作区启动，上一条命令的 `cd` 不影响下一条调用。

Windows 使用系统 `cmd.exe /d /s /c`，在独立隐藏控制台中设置 UTF-8 代码页，并用 Job Object 在超时或主命令结束时清理子进程。工作区须为盘符路径；UNC 工作目录会明确报错。外部程序仍可能自行输出其他编码，应将其配置为 UTF-8。

macOS/Linux 测试实现使用 `/bin/sh -c`，通过固定目录句柄进入工作区，超时或主命令结束时清理同一进程组。主动创建新会话、脱离进程组的程序不保证被清理；这不是用于运行不可信命令的系统沙箱。

宿主测试包含文件创建/覆盖、工作区与链接限制、固定目录、命令退出码、输出截断、超时和后台进程清理。Windows 分支仍需在 Windows 上构建并运行验证；宿主测试不能替代该验证。
