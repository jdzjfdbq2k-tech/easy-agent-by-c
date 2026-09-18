# easy-agent-by-c

一个用 C 编写的 Windows 命令行 Agent。模型通过兼容 Chat Completions 的接口请求工具，程序执行工具后将结果回填到对话，最多调用模型 20 轮。目前内置 `read_file`、`list_dir`、`write_file` 和 `run_command`。

## 阅读顺序

先读 `src/main.c` 的 Agent 循环，再依次读 `core/msgs.c`（JSON 消息历史）、`core/llm.c`（请求与响应）、`core/client.c`（HTTP）。工具部分按 `tools/tool_definition.h` → `builtins.c` → `registry.c` → 各工具实现阅读。最后看 `util/strbuf.c` 和测试。

头文件保留接口与内存所有权约定；源码注释只解释关键行为。

## 目录

```text
include/                 项目对外头文件，保持平铺
vendor/
  cJSON/                 第三方 JSON 库（cJSON.c + cJSON.h），和自己的代码隔离
src/
  main.c                 用户输入和 Agent 循环
  core/                  Agent 本体
    llm.c                模型请求与响应解析
    client.c             WinHTTP 网络请求
    msgs.c               对话历史
    env.c                 .env 加载
  util/                  基础设施
    strbuf.c             动态字符串
  tools/
    tool_definition.h    工具作者接口（内部使用）
    builtins.c           内置工具注册列表
    registry.c           schema 生成、参数解析和执行分发
    read_file.c          文件读取及其工具定义
    list_dir.c           列出目录
    write_file.c         局部编辑文本文件
    run_command.c        命令执行、超时与输出收集
    text_output.h        UTF-8 输出校验
tests/                 工具层与消息层测试
workspace/             Agent 的默认工作目录（可自动创建）
build/                 生成的程序、对象文件与测试程序（Git 忽略）
```

## 构建与运行

主程序依赖 Windows WinHTTP 和 MSYS2/MinGW 的 gcc，只能在 Windows 上构建和运行。

MinGW 自带的构建程序叫 `mingw32-make`，**不叫 `make`**（避免和其他 make 撞名），所以直接敲 `make` 会报"无法将 make 项识别为 cmdlet"。

```sh
mingw32-make                 # 生成 build/agent.exe
mingw32-make test            # 编译并运行测试
mingw32-make clean           # 删掉 build/
```

如果想让 `make` 也能用，把 `C:\developer\mingw\bin\mingw32-make.exe` 复制一份改名为 `make.exe`，或者在 PowerShell 里加一条别名：

```powershell
Set-Alias make mingw32-make
# 想永久生效就写进 $PROFILE
```

Makefile 里显式设了 `SHELL := cmd.exe`，recipe 全部是 cmd 原生语法。这样不管 PATH 里有没有 `sh.exe`（`mingw32-make` 会优先找它），行为都一致——否则同一份 Makefile 会因为 shell 不同而用上互相不兼容的 `mkdir -p` 和 `mkdir`。

复制 `.env.example` 为 `.env`，填写 `LLM_HOST`、`LLM_PATH`、`LLM_MODEL`、`LLM_API_KEY`。在 Windows 的项目根目录启动 `build/agent.exe`，确保能找到 `.env`；文件工具和命令的相对路径都以启动目录下的 `workspace/` 子目录为基准。

## 添加工具

注册方式是编译期静态列表，无需运行时初始化，也不需要修改 `main.c` 或 `llm.c`。

1. 在 `src/tools/` 新建一个 `.c` 文件，实现工具并导出 `const tool_definition`。可参考 `read_file.c`：定义名称、描述、参数 JSON Schema 和执行函数。
2. 在 `builtins.c` 声明该定义，将其地址加入 `builtin_tools`。
3. 为新工具补充行为测试，执行 `mingw32-make test`。构建配置自动收集 `src/tools/*.c`。

注册表会从同一份定义生成模型的 `tools` 数组，并按名称查找执行函数。重复名称、缺少必要元数据或执行函数会被拒绝；生成 schema 时还会检查参数定义为 `type: object`。

执行器接收已解析的 JSON 对象，自己校验字段类型及语义；不得释放或保存传入的对象。返回文本由 `malloc` 分配，调用方负责 `free`。一般错误应返回以 `error: ` 开头的文本，内存不足可返回 `NULL`。

公共接口 `tools_schema_json()` 现在返回动态生成的字符串，调用方必须 `free`，失败返回 `NULL`。`tool_run()` 拒绝非对象参数以及 JSON 后的多余内容；空字符串或 NULL 参数按 `{}` 处理。这里只检查 JSON 对象形态，不是完整的 JSON Schema 校验器。

## 工作目录与提示词约定

程序先从启动目录加载 `.env`，再创建并进入 `workspace/`。文件工具直接使用系统文件 API，命令继承这个工作目录；相对路径均从这里解析。

操作范围由 `src/main.c` 的固定系统提示词约定：默认在工作目录内操作，用户明确指定其他位置时才访问；不访问凭据或无关文件。代码不再检查路径是否位于工作区内，也不拒绝绝对路径、`..` 或符号链接。这是行为约定，不是沙箱；实际访问能力由操作系统权限决定。

`src/tools/file_io.h` 只负责 UTF-8 路径转换和文件打开，保留中文文件名支持。文件编辑仍要求 `old_string` 唯一匹配；父目录必须存在。写入不是原子替换，磁盘错误可能留下部分内容。

终端只显示工具名称，完整工具结果仍回传模型。

## 内置工具参数

| 工具 | 参数示例 | 行为 |
| --- | --- | --- |
| `read_file` | `{"path":"src/main.c"}` | 读取文件，正文最多约 4000 字节 |
| `list_dir` | `{"path":"."}` | 列出直接子项，目录以 `/` 结尾；不递归、不保证排序 |
| `write_file` | `{"path":"notes.txt","old_string":"旧文本","new_string":"新文本"}` | 把文件中唯一匹配的 `old_string` 替换为 `new_string`；文件不存在且 `old_string` 为空时新建 |
| `run_command` | `{"command":"echo hello","timeout_ms":30000}` | 执行命令，合并 stdout/stderr，附上 `exit_code` |

目录列表与命令输出的正文上限为 4000 字节，超过时附上截断标记。目录名中的控制字节、反斜杠和非法 UTF-8 字节显示为 `\xHH`；命令输出中的非法 UTF-8 或 NUL 字节以 `?` 替换。合法中文不会在截断点被切成半个字符。

命令的 `timeout_ms` 可省略，默认 30000，允许 1–120000 的整数。超时返回 `error: command timed out` 和退出码 124。标准输入接到空设备，不支持交互输入。每次调用从初始化工作区启动，上一条命令的 `cd` 不影响下一条调用。

使用系统 `cmd.exe /d /s /c`，在独立隐藏控制台中设置 UTF-8 代码页，并用 Job Object 在超时或主命令结束时清理子进程。工作区须为盘符路径；UNC 工作目录会明确报错。

注意：设置代码页只对主动查询控制台代码页的程序有效。`cmd.exe` 自己的内建命令（如 `echo`）和错误消息仍按系统 ANSI 代码页（中文 Windows 上是 GBK）写出，这些字节不是合法 UTF-8，会被替换成 `?`。需要 UTF-8 输出应改用支持该编码的外部程序。

测试覆盖命令退出码、超时、输出截断，以及文件工具的读写、目录枚举和普通路径解析。
