# elevated-kit 开发者指南

本文档只描述当前实现。SDK 支持 Windows 与 Arch Linux，使用 C++17。

## 1. 入口与公共 API

公共宿主应包含：

```cpp
#include "elevate_kit/elevate_kit.h"
```

公共类是 `elevate_kit::ElevateKit`，任务通过静态宏注册：

```cpp
REGISTER_ELEVATED_TASK("Example_Task",
                       [](const std::string& params_json) {
                           return true;
                       });
```

`REGISTER_ELEVATED_TASK` 的处理器类型为 `bool(const std::string&)`。`params_json` 是
UTF-8 不透明传输字符串，库不替其定义 schema。`module_id` 当前只作为兼容传输字段并被
忽略，不承诺动态模块加载。

### 应用 ID

```cpp
int main(int argc, char* argv[]) {
    elevate_kit::ElevateKit::setApplicationId("com.example.product");

    if (elevate_kit::ElevateKit::process(argc, argv)) return 0;
    return elevate_kit::ElevateKit::runTask("Example_Task", "{}") ? 0 : 1;
}
```

`setApplicationId()` 必须在 `process()` 或 `runTask()` 前调用。未设置或设置为空时，
平台使用当前宿主进程文件名 fallback。应用 ID 只保留 ASCII 字母、数字、`.`、`-`；
其他字符映射为 `-`，若结果为空则使用 `host`。该 ID 不含用户 SID，也不进行复杂验证。

相同 fallback 名称或相同显式 ID 会共享配置；需要共存的不同产品必须设置不同且稳定的 ID。

## 2. 模式、CLI 与返回值

实际 worker CLI 仅供 SDK 内部使用：

```text
--elevated --elevate-ipc <id>
```

worker 不执行 `--elevated <task> <params>` 形式的 direct task CLI。`--elevated-install`
是内部安装/修复路径。

`ElevateKit::process(argc, argv)` 返回 `true` 仅表示内部模式已被消费，不代表 worker 或
installer 成功；消费后宿主应退出。example 的 installer 额外根据 readiness 决定退出码，
这是参考宿主行为而不是 `process()` 的返回语义。

普通 client 示例：

```cpp
int main(int argc, char* argv[]) {
    elevate_kit::ElevateKit::setApplicationId("com.example.product");
    if (elevate_kit::ElevateKit::process(argc, argv)) return 0;

    const bool ok = elevate_kit::ElevateKit::runTask("Example_Task", "{}");
    return ok ? 0 : 1;
}
```

## 3. 平台命名与安装修复

Windows Task Scheduler 任务名为 `ElevateKit.<id>`。Arch Linux 的 action、policy、rule
基于 `com.elevatekit.<id>.elevated`。旧的固定命名不再视为 ready；实现不承诺迁移或删除旧配置。

Arch Linux 的 Polkit rule 只对当前派生 custom action 返回 `YES`。其 policy 同时固定
canonical host executable path 和首个 `--elevated` 参数，因此授权边界是每个应用 ID 的
action/path/argv1 组合，不附加用户或会话条件。授权后 Worker 仍通过 Unix socket 验证
相同 host executable、root peer 身份及 IPC 请求。

首次 Windows 修复可能触发 UAC。Arch Linux 修复需要 `pkexec`。`runTask()` 的约 3 秒预算只
适用于 worker 启动后的 IPC 连接、request/response 和 worker 完成阶段；互动安装/修复不在
该预算内。超时返回 `false` 不保证任务没有继续执行。

Windows Release 宿主路径必须位于 `%ProgramFiles%` 下。Arch Linux Release 安装布局要求宿主
位于 `/usr/libexec/`、库位于 `/usr/lib/`（以前缀 `/usr` 安装）。Debug `build/install`
布局仅用于开发验证，不应描述为 Release 免密部署；这些是本 SDK 的部署约束，不代表 Arch
默认软件包布局。

## 4. 构建、安装与公共头

Conan 提供 `fmt/10.2.1`、`nlohmann_json/3.11.3`；测试还需要 `doctest/2.4.11`。CMake
要求 3.25 或更新版本。

Windows Debug 示例：

```powershell
conan install . -of build -s build_type=Debug -o shared=True -o test=True -o example=True --build=missing
cmake -S . -B build -G "Ninja Multi-Config" `
  -DCMAKE_TOOLCHAIN_FILE=build/generators/conan_toolchain.cmake `
  -DBUILD_SHARED_LIBS=ON -DBUILD_TEST=ON -DBUILD_EXAMPLE=ON
cmake --build build --config Debug
cmake --install build --config Debug
ctest --test-dir build -C Debug --output-on-failure
& .\build\install\bin\example.exe
```

Windows Debug 的 `example.exe` 与 DLL 位于 `build/install/bin`。公共头安装在
`build/install/include/elevate_kit/`，包括 `elevate_kit.h` 与 `export.h`。

### Arch Linux

```sh
conan install . -of build -s build_type=Release -o shared=True -o test=True -o example=True --build=missing
cmake -S . -B build -G "Ninja Multi-Config" \
  -DCMAKE_TOOLCHAIN_FILE=build/generators/conan_toolchain.cmake \
  -DBUILD_SHARED_LIBS=ON -DBUILD_TEST=ON -DBUILD_EXAMPLE=ON
cmake --build build --config Release
cmake --install build --config Release
ctest --test-dir build -C Release --output-on-failure
./build/install/libexec/example
```

Arch Linux 使用上述 Conan/CMake 命令；Release host 应从 `build/install/libexec`
（部署前缀为 `/usr` 时即 `/usr/libexec`）运行，库位于对应的 `lib` 目录。验证依赖包括
systemd、polkit、pkexec、CMake、GCC、Ninja 和 Conan；不提供自动安装逻辑。这是本 SDK 的
部署约束，不代表 Arch 默认软件包布局。

## 5. Windows console 缓解方案

`FreeConsole()` 只是参考宿主方案。Windows 宿主可在 `main()` 最前面、任何 worker 输出
以及 `ElevateKit::process()` 前，仅对首参数精确为 `--elevated` 的 worker 调用它：

```cpp
#ifdef _WIN32
#include <windows.h>
#endif

int main(int argc, char* argv[]) {
#ifdef _WIN32
    if (argc >= 2 && argv[1] != nullptr &&
        std::string(argv[1]) == "--elevated") {
        FreeConsole();
    }
#endif
    // setApplicationId() 仍应在 process()/runTask() 前调用
    if (elevate_kit::ElevateKit::process(argc, argv)) return 0;
}
```

它只能缩短 worker console 闪现，不能保证无窗口；worker 断开 console 后 stdout/stderr
诊断不可用。完全无窗口需要 GUI host 或 GUI worker。

## 6. 验证范围

- Windows Debug 已人工验证 UAC repair、Task Scheduler/RunEx、Named Pipe 完整成功。
- Windows Release 未验证。
- Arch WSL 已完成 action-only Polkit rule 下的 strict Debug/Release build、install、CTest、policy/rule
  installer 及 ordinary-user pkcheck YES、pkexec Worker、Unix Socket、static handler response 和 exit 0
  端到端验证；测试 policy/rule 已清理。
- CTest 仅覆盖协议与 CLI 单测，不覆盖真实 UAC、Task Scheduler、Polkit 或端到端部署。
