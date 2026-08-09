# elevate-kit

> 一个用于安全调用已注册特权任务的轻量、跨平台 C++17 库。

[English](README.md) · [简体中文](README.zh-CN.md)

`elevate-kit` 是一个 C++17 特权任务调用库。应用在宿主进程中静态注册接收 JSON 的任务，普通进程通过本地 IPC 请求特权进程执行任务并取得 JSON 结果。

当前支持 **Windows** 和 **Linux**；不支持 macOS 或其他平台。

## 目录

- [功能](#功能)
- [依赖](#依赖)
- [构建、安装与运行](#构建安装与运行)
- [最小 API 示例](#最小-api-示例)
- [应用 ID](#应用-id)
- [日志回调](#日志回调)
- [生产部署约束](#生产部署约束)

## 功能

- **单一宿主程序**：无需额外部署常驻服务或独立的提权助手；同一宿主程序按需以特权进程启动并执行任务。提权业务逻辑与普通业务逻辑可保持在同一代码库和模块中，无需拆分、同步或维护另一套特权服务逻辑。
- **少量接入代码**：注册一个 JSON 任务并调用 `CallTask`，即可将敏感操作交给特权进程处理，无需自行维护 IPC 协议。
- **按需获得权限**：仅在调用特权任务时启动特权进程，避免常驻高权限服务扩大攻击面。
- **统一跨平台接口**：Windows 和 Linux 使用相同的 C++ API，屏蔽各平台提权与进程通信差异。
- **受限的特权操作**：特权进程仅执行显式注册的任务，而非通用命令执行入口；调用参数和结果均使用 JSON，便于定义和审计业务操作。
- **多层通信校验**：每次调用使用独立 IPC 会话，并校验通信对端的进程身份和宿主路径，降低本地进程冒充风险。
- **安全部署约束**：Windows Release 强制宿主位于 Program Files；Linux Release 要求宿主由 root 所有且不可被所有用户写入。
- **适配模块化应用**：支持从 DLL/SO 加载静态注册的任务，适用于插件或模块拆分的应用。

## 依赖

- CMake 3.25 或更新版本；
- C++17 编译器和 Ninja；
- Conan；

> **前置条件：** Windows 需要可用的 MSVC/Visual Studio C++ 工具链。Arch Linux 的真实特权流程还需要 `systemd`、`polkit` 和 `pkexec`。本文不提供系统依赖的自动安装命令。

## 构建、安装与运行

```sh
conan install . -o example=True -o test=True
cmake --preset conan-default --fresh
cmake --build build --target install
./build/install/bin/example
```

默认安装前缀是 `build/install`。公共头文件位于 `include/elevate_kit/`，库位于 `lib/`，示例和其他运行时文件位于 `bin/`。构建时不需要测试或示例时，可关闭 `test`、`example` 选项，并省略对应的 `BUILD_TEST`、`BUILD_EXAMPLE` CMake 选项。

要安装到系统位置，应在生成的 CMake 预设中配置 `CMAKE_INSTALL_PREFIX`（例如 `/usr`），再执行安装。安装后的库、依赖和运行时文件必须一并部署。

## 最小 API 示例

以下示例与仓库中的 `example/main.cpp` 使用相同的 API 形式：

```cpp
#include "elevate_kit/elevate_kit.h"

#include <filesystem>
#include <fstream>

REGISTER_ELEVATED_TASK(example_task, [](const nlohmann::json &params) {
    const auto filename = params.at("filename").get<std::string>();
    const auto path = std::filesystem::temp_directory_path() / filename;
    std::ofstream(path) << "created by elevate-kit\n";
    return nlohmann::json{{"filesize", std::filesystem::file_size(path)}};
});

int main(int argc, char *argv[]) {
    elevate_kit::Process(argc, argv);

    const auto result = elevate_kit::CallTask(
        "example_task", nlohmann::json{{"filename", "elevate-kit.txt"}});
    if (result.is_null()) {
        return 1;  // 调用失败
    }
    return 0;
}
```

任务处理器的类型是 `std::function<nlohmann::json(const nlohmann::json&)>`。`CallTask(task_name, params)` 成功时返回任务产生的 JSON，失败时返回 **null JSON**；不要将返回值当作 `bool` 或 `std::string` 处理。需要显式提供模块路径时，可使用公开的三参数 `CallTask` 重载。

`Process(int argc, char *argv[])` 返回 `void`。普通进程应调用它后继续执行自己的逻辑；它处理库使用的特权进程和安装流程启动场景。若应用本身以特权进程启动，处理完成后会退出。

## 应用 ID

```cpp
elevate_kit::SetApplicationId("com.example.product");
```

应在 `Process` 或 `CallTask` 前设置稳定且唯一的应用 ID。它用于区分平台上的应用配置；不同产品必须使用不同 ID。ID 只能包含 ASCII 字母、数字、下划线（`_`）、连字符（`-`）和点（`.`），不得包含空格或其他特殊字符；建议使用 `com.example.product` 这类稳定名称。未设置或传入空值时，库使用宿主文件名作为 fallback。

## 日志回调

可以为 debug、info、warn、error 四个级别注入回调：

```cpp
void OnLog(std::string_view message) {
    // message 仅在本次回调期间有效
}

elevate_kit::SetLogCallbacks({OnLog, OnLog, OnLog, OnLog});
// 恢复默认 stdout/stderr 输出：
elevate_kit::ResetLogCallbacks();
```

未提供的回调使用默认输出：debug/info 写入 stdout，warn/error 写入 stderr。回调不应抛出异常，并且必须保证自身生命周期；特权进程独立运行，如需接收其日志，需在特权进程启动前配置回调。

## 生产部署约束

> **部署说明：** 以下要求适用于生产部署。

- Windows 的 Release elevated host 必须安装在 `%ProgramFiles%` 下；首次修复可能显示 UAC。
- Windows 建议使用 GUI 子系统应用作为宿主；控制台应用以特权进程启动时可能出现短暂的黑框闪现。
- Linux 特权进程必须存在且不可被所有用户写入；Release 下还必须由 root 所有。Polkit 会绑定配置时的特权进程绝对路径，并将配置写入 `/usr/share/polkit-1/actions/` 和 `/etc/polkit-1/rules.d/`。
- `CallTask` 的超时或 null JSON 只表示本次调用未成功返回结果，不保证任务没有开始或继续执行。
