# 静态内置工具结构设计

## 状态

已确认：先采用最简单的静态内置工具方案，不实现动态插件、工具沙箱或额外的上下文抽象。

## 目标

- 让 Agent 能通过一个统一入口执行多个内置工具。
- 工具定义、参数 Schema 和执行函数放在同一个静态注册表中。
- 新增工具时只需要增加一个处理函数和一条注册表记录。
- 继续使用项目现有的 cJSON，不引入新的 JSON 抽象层。

## 非目标

- 动态加载 DLL 或独立工具进程。
- 工具权限、工作区沙箱和路径安全策略。
- 多线程工具执行。
- 把工具系统拆成复杂的插件框架。

## 结构

当前阶段保持最小文件结构：

```text
include/
  tools.h

src/
  tools.c
```

`read_file` 暂时继续放在 `src/tools.c`。当工具数量达到约 3～4 个时，再拆成 `src/tools/registry.c`、`read_file.c` 等文件；公共调用方式保持不变。

## 接口

工具直接使用 cJSON：

```c
typedef char *(*tool_handler)(const cJSON *args);

typedef struct {
    const char *name;
    const char *description;
    const char *parameters_json;
    tool_handler handler;
} tool_definition;

char *tool_run(const char *name, const char *arguments_json);
const char *tools_schema_json(void);
```

`tool_run()` 负责解析参数、查找工具、调用处理函数、释放 cJSON 参数对象并返回工具输出。工具失败时返回以 `error:` 开头的字符串，而不是返回 `NULL`。

## 注册表

`src/tools.c` 内维护静态表：

```c
static const tool_definition tools[] = {
    {
        "read_file",
        "Read a text file",
        READ_FILE_PARAMETERS_JSON,
        tool_read_file
    }
};
```

`tools_schema_json()` 遍历同一张表生成发送给模型的工具定义，避免工具 Schema 和实际分发逻辑出现两份名单。

## 运行流程

```text
模型返回 tool_call
        |
        v
main.c -> tool_run(name, arguments_json)
        |
        +-- cJSON_Parse()
        +-- 查找静态注册表
        +-- 调用 tool_handler
        +-- cJSON_Delete()
        v
返回字符串并写入 role=tool 消息
```

`tool_call` 仍然表示模型返回的数据；`tool_definition` 表示本地工具能力。两者概念不同，但当前阶段可以继续放在同一个 `tools.h` 中。

## 错误约定

- `name == NULL`：返回错误字符串。
- 参数为空：按 `{}` 处理。
- 参数不是合法 JSON：返回错误字符串。
- 工具名不存在：返回错误字符串。
- 工具参数缺失或类型错误：由具体工具返回错误字符串。
- 工具输出由调用方负责 `free()`。

## 测试范围

继续使用当前平台无关测试，并增加静态注册表相关断言：

- Schema 是合法 JSON 数组。
- 注册表能找到 `read_file`。
- 未知工具返回错误字符串。
- 新工具注册后能被 `tool_run()` 调用。
- 工具参数错误不会导致进程崩溃。

网络层和 LLM 调用不纳入工具单元测试。
