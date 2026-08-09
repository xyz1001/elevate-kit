# elevate-kit

`elevate-kit` 是一个 C++17 的 Windows/Linux 特权任务调用库。任务处理器在宿主进程内通过静态宏注册；普通 client 通过本地 IPC 请求同一宿主的 elevated worker 执行任务。

当前实现支持 Windows 与 Arch Linux。`params_json` 是 UTF-8 不透明传输字符串；`module_id` 目前仅为兼容传输字段并被忽略，不提供动态模块加载。

## 构建与安装

依赖由 Conan 提供：`fmt/10.2.1`、`nlohmann_json/3.11.3`；启用测试时还需要 `doctest/2.4.11`。CMake 要求 3.25 或更新版本，标准为 C++17。

### Windows Debug

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

Windows Debug 的示例宿主和共享 DLL 位于 `build/install/bin`。公共头安装在
`build/install/include/elevate_kit/`。该 Debug 布局用于开发验证，不是 Release 免密部署步骤。

### Windows Release 部署约束

将 Conan、构建、安装和 CTest 命令中的配置改为 `Release`。Release 宿主必须位于
`%ProgramFiles%` 下；库和运行时文件应按实际安装结果部署。

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

Arch 验证依赖 systemd、polkit、pkexec、CMake、GCC、Ninja 和 Conan；本文不提供自动安装逻辑。
Release Linux host 的安装布局是 `/usr/libexec/`，库是 `/usr/lib/`（使用安装前缀为
`/usr` 的部署）。这是本 SDK 的部署约束，不代表 Arch 默认软件包布局。

## 最小 API

```cpp
#include "elevate_kit/elevate_kit.h"

REGISTER_ELEVATED_TASK("Example_Task",
                       [](const std::string& params_json) { return true; });

int main(int argc, char* argv[]) {
    elevate_kit::ElevateKit::setApplicationId("com.example.product");

    if (elevate_kit::ElevateKit::process(argc, argv)) return 0;
    return elevate_kit::ElevateKit::runTask("Example_Task", "{}") ? 0 : 1;
}
```

`setApplicationId()` 必须在 `process()` 或 `runTask()` 前调用。未设置或传入空值时，
平台使用当前宿主进程文件名作为 fallback；应用 ID 规范化后只保留 ASCII 字母、数字、
`.`、`-`，其他字符映射为 `-`，空结果使用 `host`。应用 ID 不包含用户 SID。

同名 fallback 或相同显式 ID 会共享平台配置；需要共存的不同产品必须设置不同且稳定的 ID。

`REGISTER_ELEVATED_TASK` 是静态注册宏，处理器类型为 `bool(const std::string&)`。
公共 API 位于 `elevate_kit/elevate_kit.h`，类名是 `elevate_kit::ElevateKit`。

## 内部模式与返回值

真实 worker CLI 仅供内部使用：

```text
--elevated --elevate-ipc <id>
```

它不执行 `--elevated <task> <params>` 形式的 direct task CLI。`--elevated-install` 是内部安装/修复模式。

`ElevateKit::process()` 返回 `true` 只表示内部模式已被消费，不代表 worker 或 installer 成功；
消费后宿主必须退出。example 的 installer 路径是额外的参考实现，会根据 readiness 决定退出码。

首次 Windows 修复可能显示 UAC；Arch Linux 修复需要 `pkexec`。`runTask()` 的约 3 秒预算只覆盖
worker 启动后的 IPC 连接、request/response 和 worker 完成阶段；互动安装/修复不属于该预算。
超时返回 `false` 不保证任务没有继续执行。

Windows Task Scheduler 使用 `ElevateKit.<id>` 命名任务；Arch Linux action/policy/rule 基于
`com.elevatekit.<id>.elevated`。旧的固定命名不会被视为 ready，也不承诺迁移或删除。

Arch Linux 的 Polkit 授权边界是每个应用 ID 派生的 custom action：policy 将该 action
绑定到 canonical host executable path，并要求首个参数为 `--elevated`；对应 rule 只授权
匹配该 action。它不按用户或会话条件限制授权。授权后的 Worker 仍通过 Unix socket
验证相同的 canonical host、root peer 身份和 IPC 请求。

## Windows console 限制

`FreeConsole()` 仅是参考宿主方案：worker 入口可在 Windows 下、任何 worker 输出和
`process()` 前，仅对首参数精确为 `--elevated` 的进程调用它，以缩短 console 闪现。
它不保证无窗口，且 worker 的 stdout/stderr 诊断不可用；完全无窗口需要 GUI host 或 GUI worker。

## 验证状态

- Windows Debug 已人工验证 UAC repair、Task Scheduler/RunEx 和 Named Pipe 完整成功。
- Windows Release 尚未验证。
- Arch WSL 已完成 action-only Polkit rule 下的 strict Debug/Release build、install、CTest、policy/rule
  installer 及 ordinary-user pkcheck YES、pkexec Worker、Unix Socket、static handler response 和 exit 0
  端到端验证；测试 policy/rule 已清理。
- CTest 仅覆盖协议与 CLI 单元测试，不等同于真实 UAC、Task Scheduler、Polkit 或端到端验证。
