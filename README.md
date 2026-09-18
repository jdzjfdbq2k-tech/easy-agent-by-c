# easy-agent-by-c

一个用 C 编写的 Windows 命令行 Agent。模型通过兼容 Chat Completions 的接口请求工具，程序执行工具后将结果回填到对话，最多调用模型 20 轮。目前内置 `read_file`。

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
tests/                  平台无关测试
build/                  生成的程序、对象文件与测试程序（Git 忽略）
```

## 构建与运行

主程序依赖 Windows WinHTTP。Windows 可使用 MSYS2 MinGW；macOS/Linux 安装 mingw-w64 后可以交叉编译，但生成的程序仍须在 Windows 上运行。

```sh
make                         # 生成 build/agent.exe
make test                    # 本机运行平台无关测试，无需 MinGW
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

当前提供只读入口。后续写入、创建或删除工具也应在这个模块中增加对应的受控操作，不能用只读检查结果再自行 `fopen` 写入。本模块是工具层的访问约束，不是操作系统沙箱。
