# 🛡️ elevated-kit 商业级无感提权架构设计与安全规范

---

## 目录
- [一、 项目定位与安全基线对标](#一-项目定位与安全基线对标)
- [二、 架构设计与安全责任划分](#二-架构设计与安全责任划分)
- [三、 核心技术解法与工程落地细节](#三-核心技术解法与工程落地细节)
  - [1. 免密服务就绪状态与参数匹配检测机制 (isPasswordlessReady)](#1-免密服务就绪状态与参数匹配检测机制-ispasswordlessready)
  - [2. Windows 管道安全标志与 GetNamedPipeServerProcessId PID 核验](#2-windows-管道安全标志与-getnamedpipeserverprocessid-pid-核验)
  - [3. Task Scheduler 2.0 COM API (IRegisteredTask::RunEx) 动态传参](#3-task-scheduler-20-com-api-iregisteredtaskrunex-动态传参)
  - [4. Linux Unix Domain Socket (SO_PEERCRED) 凭证核验与资源清理](#4-linux-unix-domain-socket-so_peercred-凭证核验与资源清理)
  - [5. 逻辑 moduleId 安全路径映射 (防止危险路径动态加载)](#5-逻辑-moduleid-安全路径映射-防止危险路径动态加载)
  - [6. CI/CD DevSecOps 流水线与全量 Action 静态审查](#6-cicd-devsecops-流水线与全量-action-静态审查)
  - [7. 结构化日志审计与 request_id 追踪](#7-结构化日志审计与-request_id-追踪)
- [四、 开发调试与生产发布隔离规范](#四-开发调试与生产发布隔离规范)
- [五、 结论](#五-结论)

---

## 一、 项目定位与安全基线对标

**`elevated-kit`** 是一个专为跨平台（Windows / Linux）桌面客户端（C++ / Qt）设计的**有限作用域业务配置提权 Broker**。

### 安全基线对标 (Security Parity Baseline)
本方案以**业界主流桌面软件（如 VS Code Helper / Docker Desktop / VPN Service 架构）**为安全对标基线：
* **核心防线**：提权 Worker 坚决不提供任何“执行任意命令”或“写任意文件”的通用接口，仅允许执行强类型的业务 Action。
* **后果边界**：即使普通权限的 Daemon 进程被同用户下的恶意进程入侵或操控，攻击后果被严格锁定在已知的业务设置范围内，**SYSTEM / root 权限绝无失控或被用于任意代码执行 (LPE) 的可能**。
* **暴露面优势**：与传统 24 小时常驻后台的高权限服务相比，`elevated-kit` 的特权 Worker **随用随起，执行完配置修改后 0.1 秒内瞬间 exit(0) 销毁**，攻击暴露窗口远小于传统方案。

---

## 二、 架构设计与安全责任划分

根据“高内聚、低耦合”的软件工程原则，`elevated-kit` 明确划分了 SDK 框架与业务组件库的安全责任：

* **SDK 框架层责任**：
  * 提供统一高聚拢的免密服务就绪检测 API (`ElevateKit::isPasswordlessReady`)；
  * 提供随机 UUID 管道建立与 Task Scheduler 2.0 `RunEx` / `pkexec` 动态传参；
  * 提供安全的跨进程 IPC 管道通信（设置 `PIPE_REJECT_REMOTE_CLIENTS` 与 `FILE_FLAG_FIRST_PIPE_INSTANCE`）与 Windows/Linux 内核级 PID 校验；
  * 提供逻辑 `moduleId` 安全路径映射表，封死危险路径 DLL 加载；
  * 提供结构化日志审计与 `request_id` 追踪；
  * 结合 CI/CD DevSecOps 静态扫描工具进行自动化安全治理。
* **组件库 (DLL/SO) 责任**：
  * 定义具体的业务 Action 逻辑；
  * 遵循“谁最懂业务谁校验”原则，在其注册的 Lambda 内部进行参数强类型 Schema 校验与边界控制。

---

## 三、 核心技术解法与工程落地细节

### 1. 免密服务就绪状态与参数匹配检测机制 (isPasswordlessReady)
为防止提权调用因计划任务缺失、路径错位或软硬件升级后的参数失效而崩溃，SDK 框架层提供了统一的免密就绪检测 API `ElevateKit::isPasswordlessReady()`。

该接口同时校验**安装状态**与**配置参数对应关系**，任意一项不匹配均直接返回 `false`，要求重新安装/触发弹窗修复：

```cpp
// 统一高聚拢免密检测 API (返回值: true=就绪可静默提权; false=未安装或参数不匹配需修复)
static bool ElevateKit::isPasswordlessReady();
```

#### 底层判定逻辑：
* **Windows**：通过 Task Scheduler 2.0 COM API `ITaskFolder::GetTask(_bstr_t(L"MyApp_Worker"), ...)` 查询计划任务。
  1. 若任务不存在 $\rightarrow$ 直接返回 `false`；
  2. 若任务存在，解析 `<Exec><Command>` 与 `<WorkingDirectory>`，校验是否精准匹配当前的绝对路径（如 `C:\Program Files\MyApp\daemon.exe`）及 `WorkingDirectory`；若路径或参数不匹配 $\rightarrow$ 返回 `false`；
  3. 仅当任务存在且参数完全对应 $\rightarrow$ 返回 `true`。
* **Linux**：读取 `/etc/polkit-1/rules.d/50-myapp-worker.rules` 规则文件。
  1. 若文件不存在 $\rightarrow$ 返回 `false`；
  2. 若文件存在但绑定的程序路径不等于 `/usr/libexec/myapp-daemon` $\rightarrow$ 返回 `false`；
  3. 规则存在且参数正确 $\rightarrow$ 返回 `true`。

---

### 2. Windows 管道安全标志与 GetNamedPipeServerProcessId PID 核验
* 特权 Worker 作为管道 Client 发起连接，Daemon 作为管道 Server 监听动态 UUID 管道。
* 管道创建时显式指定 **`PIPE_REJECT_REMOTE_CLIENTS`**（拒绝远程连接）与 **`FILE_FLAG_FIRST_PIPE_INSTANCE`**（防止管道抢占）。
* Worker 连接建立后，直接调用 Win32 原生 `GetNamedPipeServerProcessId(hPipe, &serverPid)` API 取得 Daemon 的 PID。
* Worker 进一步通过 `QueryFullProcessImageNameW` 查询该 PID 的可执行文件绝对路径，确认其位于受保护的 `Program Files` 目录下，核验通过后方可建立通信。

### 3. Task Scheduler 2.0 COM API (IRegisteredTask::RunEx) 动态传参
* 废弃命令行的 `schtasks /run`。
* 静默触发时直接调用 C++ 原生 COM API `IRegisteredTask::RunEx` 传入动态 `BSTR` 数组（仅影响本次运行，不修改任务定义）。
* 采用 `_com_ptr_t` (RAII) 智能指针管理 COM 资源，防止内存泄露。

### 4. Linux Unix Domain Socket (SO_PEERCRED) 凭证核验与资源清理
* Linux 平台上使用 Socket 选项 **`SO_PEERCRED`** 获得调用方真实 PID 及 UID。
* Worker 读取 `/proc/$PID/exe` 的符号链接解析真实绝对路径，确认其处于 `/usr/` 或 `/opt/` 受保护系统路径下。
* 处理完 IPC 通信后，显式调用 `unlink()` 销毁 Socket 临时文件。

### 5. 逻辑 moduleId 安全路径映射 (防止危险路径动态加载)
* JSON 请求中严禁传输原始磁盘路径字符串。
* 客户端仅能传入逻辑模块名（如 `"net_module"`）。
* 特权 Worker 内部只从硬编码的模块映射表解析路径：
  * Windows 映射为 `C:\Program Files\MyApp\modules\net_module.dll`；
  * Linux 映射为 `/usr/libexec/myapp/libnet_module.so`。
* 未注册的 `moduleId` 一律拒绝加载，彻底封死任意 DLL/SO 侧载漏洞。

### 6. CI/CD DevSecOps 流水线与全量 Action 静态审查
* 在 CI/CD 编译构建流水线中引入静态扫描工具，自动提取工程中所有 `REGISTER_ELEVATED_TASK` 的定义，生成 JSON/Markdown 格式的《全量特权 Action 目录与 Schema 审计清单》，供安全部门审查。

### 7. 结构化日志审计与 request_id 追踪
审计日志采用结构化 JSON 格式写入系统事件日志。

---

## 四、 开发调试与生产发布隔离规范

### 1. 本地开发调试模式 (Debug Build `#ifndef NDEBUG`)
* **零代码包袱**：开发调试代码通过 `#ifndef NDEBUG` 条件隔离。
* 开发者在 IDE 中按 F5 调试时，无需打包安装，支持在任意本地构建目录下进行功能调测。

### 2. 生产发布模式 (Release Build `#ifdef NDEBUG`)
* **零 C++ 安全代码侵入**：定义 `NDEBUG` 预编译宏，彻底擦除调试逻辑。
* 利用安装包配置（Windows 计划任务 `WorkingDirectory`）和 Manifest 清单在 PE 加载前锁定 CWD，天然消灭 DLL 侧载漏洞。

---

## 五、 结论

本规范已完成精简高聚拢免密检测 API `ElevateKit::isPasswordlessReady()`、动态 UUID 随机管道防重名冲突、Windows Task Scheduler 2.0 `IRegisteredTask::RunEx` 动态传参（RAII 管理）、`PIPE_REJECT_REMOTE_CLIENTS` 管道安全标志、逻辑 `moduleId` 安全路径映射、Linux `SO_PEERCRED` 凭证核验与 Polkit 规则语法修正等重构设计，具备完整的上线与工程实现条件。
