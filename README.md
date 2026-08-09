# elevate-kit

> A small, cross-platform C++17 library for safely invoking registered privileged tasks.

[English](README.md) · [简体中文](README.zh-CN.md)

`elevate-kit` is a C++17 library for invoking privileged tasks. An application statically registers tasks that receive JSON in the host process; a regular process requests the privileged process through local IPC to execute a task and obtain a JSON result.

Currently supports **Windows** and **Linux**; macOS and other platforms are not supported.

## Contents

- [Features](#features)
- [Dependencies](#dependencies)
- [Build, install, and run](#build-install-and-run)
- [Minimal API example](#minimal-api-example)
- [Application ID](#application-id)
- [Logging callbacks](#logging-callbacks)
- [Production deployment requirements](#production-deployment-requirements)

## Features

- **Single host application**: No additional resident service or separate elevation helper needs to be deployed; the same host application starts as a privileged process on demand and executes tasks. Privileged and regular business logic can remain in the same codebase and modules, without splitting, synchronizing, or maintaining another privileged service implementation.
- **Small integration footprint**: Register a JSON task and call `CallTask` to hand sensitive operations to the privileged process, without maintaining an IPC protocol yourself.
- **On-demand privileges**: The privileged process starts only when a privileged task is called, avoiding the expanded attack surface of a permanently running high-privilege service.
- **Consistent cross-platform interface**: Windows and Linux use the same C++ API, hiding differences in elevation and process communication between platforms.
- **Restricted privileged operations**: The privileged process executes only explicitly registered tasks, rather than providing a general-purpose command execution entry point; call parameters and results both use JSON, making business operations easy to define and audit.
- **Multi-layer communication validation**: Each call uses an independent IPC session and validates the peer process identity and host path, reducing the risk of local process impersonation.
- **Secure deployment requirements**: Windows Release requires the host to be located under Program Files; Linux Release requires the host to be owned by root and not writable by all users.
- **Modular application support**: Statically registered tasks can be loaded from DLL/SO, supporting applications split into plugins or modules.

## Dependencies

- CMake 3.25 or newer;
- A C++17 compiler and Ninja;
- Conan;

> **Prerequisites:** Windows requires an available MSVC/Visual Studio C++ toolchain. The real privileged flow on Arch Linux also requires `systemd`, `polkit`, and `pkexec`. This document does not provide commands for automatically installing system dependencies.

## Build, install, and run

```sh
conan install . -o example=True -o test=True
cmake --preset conan-default --fresh
cmake --build build --target install
./build/install/bin/example
```

The default installation prefix is `build/install`. Public headers are located in `include/elevate_kit/`, the library in `lib/`, and examples and other runtime files in `bin/`. When tests or examples are not needed during the build, the `test` and `example` options can be disabled and the corresponding `BUILD_TEST` and `BUILD_EXAMPLE` CMake options omitted.

To install to a system location, configure `CMAKE_INSTALL_PREFIX` (for example, `/usr`) in the generated CMake preset, then perform the installation. The installed library, dependencies, and runtime files must be deployed together.

## Minimal API example

The following example uses the same API form as the repository's `example/main.cpp`:

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

The task handler type is `std::function<nlohmann::json(const nlohmann::json&)>`. On success, `CallTask(task_name, params)` returns the JSON produced by the task; on failure, it returns **null JSON**; do not treat the return value as a `bool` or `std::string`. When an explicit module path is needed, use the public three-argument `CallTask` overload.

`Process(int argc, char *argv[])` returns `void`. A regular process should call it and then continue with its own logic; it handles the privileged-process and installation-flow launch scenarios used by the library. If the application itself is launched as a privileged process, it exits after processing.

## Application ID

```cpp
elevate_kit::SetApplicationId("com.example.product");
```

Set a stable and unique application ID before `Process` or `CallTask`. It distinguishes application configurations on the platform; different products must use different IDs. The ID may contain only ASCII letters, digits, underscores (`_`), hyphens (`-`), and periods (`.`); it must not contain spaces or other special characters. A stable name such as `com.example.product` is recommended. If it is not set or an empty value is passed, the library uses the host filename as a fallback.

## Logging callbacks

Callbacks can be injected for the four levels: debug, info, warn, and error:

```cpp
void OnLog(std::string_view message) {
    // message 仅在本次回调期间有效
}

elevate_kit::SetLogCallbacks({OnLog, OnLog, OnLog, OnLog});
// 恢复默认 stdout/stderr 输出：
elevate_kit::ResetLogCallbacks();
```

Callbacks that are not provided use the default output: debug/info are written to stdout, and warn/error to stderr. Callbacks should not throw exceptions and must ensure their own lifetime; the privileged process runs independently, so callbacks must be configured before it starts if its logs are needed.

## Production deployment requirements

> **Deployment notes:** The following requirements apply to production deployment.

- The Windows Release elevated host must be installed under `%ProgramFiles%`; the first repair may display UAC.
- Windows recommends using a GUI-subsystem application as the host; a console application may briefly flash a black window when launched as a privileged process.
- The Linux privileged process must exist and not be writable by all users; in Release it must also be owned by root. Polkit binds the absolute path of the privileged process at configuration time and writes the configuration to `/usr/share/polkit-1/actions/` and `/etc/polkit-1/rules.d/`.
- A `CallTask` timeout or null JSON only means that this call did not successfully return a result; it does not guarantee that the task did not start or continue executing.
