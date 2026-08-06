# HotWordSystem

HotWordSystem 是一个基于 C++20 的实时文本流分析服务。程序接收命令行或 HTTP 请求中的文本，进行 UTF-8 清洗和中文分词，维护滑动窗口内的词频，并通过内置 Web 页面提供查询和配置接口。

项目当前以可复现构建和现有功能说明为主，不对性能、吞吐量或生产适用性作额外承诺。

## 核心功能

- 使用 `cppjieba` 分词，使用 `utfcpp` 处理 UTF-8 字符。
- 支持停用词、词性和敏感词过滤，并可通过 HTTP 接口更新部分配置。
- `SlidingWindow` 使用保留数据和当前窗口视图分离的结构：保留数据按时间存储，当前视图维护窗口内的词频。
- 使用最小堆计算 Top-K，支持调整窗口大小后从保留数据重建当前视图。
- 通过 `httplib` 提供 HTTP 服务和静态 Web 资源，通过 `nlohmann/json` 处理 JSON。
- 使用 `std::shared_mutex` 保护滑动窗口读写，使用 `spdlog` 记录持久化模块日志。
- 支持历史日志查询、简单报告生成、运行状态查询和窗口/保留时间配置。

## 简单架构

`SystemContext` 负责组织 `TextProcessor`、`SlidingWindow`、`PersistenceManager`、`PerformanceMonitor` 和线程池。主程序保留命令行输入循环，并在独立线程中启动 `APIServer`。Web 资源位于 `web/`，CMake 配置时会将 `third_party/cppjieba/dict/` 和 `web/` 复制到构建目录。

## 目录

```text
HotWordSystem/
├── CMakeLists.txt
├── include/                         # 核心模块头文件
├── src/                             # 程序入口和 SystemContext 实现
├── web/                             # Web 页面、样式和脚本
├── third_party/                     # Git submodule 依赖
└── .github/workflows/build.yml      # Ubuntu/Windows 构建检查
```

当前仓库根目录没有项目级 `test/` 或 `scripts/` 目录。

## 获取依赖

新克隆仓库时使用递归 submodule：

```bash
git clone --recurse-submodules https://github.com/DDJang/HotWordSystem.git
cd HotWordSystem
```

已有仓库初始化或补齐依赖：

```bash
git submodule update --init --recursive
```

依赖及其目录如下：

| 目录 | 上游项目 | 用途 |
| --- | --- | --- |
| `third_party/cppjieba` | `yanyiwu/cppjieba`（含递归的 `limonp`） | 中文分词 |
| `third_party/utfcpp` | `nemtrif/utfcpp` | UTF-8 处理 |
| `third_party/compile-time-regular-expressions` | `hanickadot/compile-time-regular-expressions` | 编译时正则表达式 |
| `third_party/spdlog` | `gabime/spdlog` | 日志 |
| `third_party/mimalloc` | `microsoft/mimalloc` | 内存分配器 |
| `third_party/httplib` | `yhirose/cpp-httplib` | HTTP 服务 |
| `third_party/json` | `nlohmann/json` | JSON 序列化 |

超级项目记录每个 submodule 的具体 commit；构建不依赖本机预装的这些第三方库。

## 构建

前置条件：支持 C++20 的编译器、CMake 3.18 或更高版本，以及 Git。配置和构建命令在 Linux、macOS 和 Windows 的 CMake 工作流中保持一致：

```bash
cmake -S . -B build -DCMAKE_BUILD_TYPE=Release
cmake --build build --config Release --parallel
```

CMake 会将字典和 Web 资源复制到 `build/`。程序使用相对路径读取这些资源，建议从构建目录作为工作目录启动：

```bash
cd build

# Windows（多配置生成器）
.\Release\HotWordSystem.exe

# Linux 或 macOS（单配置生成器）
./HotWordSystem
```

服务默认监听 `0.0.0.0:8080`，浏览器访问 [http://localhost:8080](http://localhost:8080)。

## HTTP 接口和命令

当前后端代码提供以下主要接口：

- `GET /api/data`
- `POST /api/command`
- `GET /api/stats`
- `GET/POST /api/config`
- `GET /api/history_view`
- `GET /api/trends`

命令行和 `/api/command` 使用同一套命令处理逻辑，常用格式包括：

```text
[12:00:00] 文本内容
[ACTION] SET_WINDOW S=600
[ACTION] SET_RETENTION R=3600
[ACTION] QUERY K=5
[ACTION] TREND K=5
[ACTION] STATS
[ACTION] RESET
[ACTION] SHUTDOWN
```

不带时间戳的文本会使用递增的时间戳。历史日志写入运行目录下的 `data/history.log`；`HISTORY` 命令生成 `report_output.txt`。

## 测试状态

当前仓库没有根目录 `test/`，CMake 也没有注册项目测试目标，因此目前没有可运行的项目级 CTest 测试。CI 会在仓库出现 `test/CMakeLists.txt` 时再执行 CTest；当前 CI 只验证 Ubuntu 和 Windows 的 Release 配置与构建。

没有为了填补测试目录而新增临时测试或声称已有测试结果。

当前本地环境没有 Linux、WSL、Docker 或 Linux 虚拟机。Ubuntu 状态：**等待 GitHub Actions 验证**。是否通过以 GitHub Actions 的实际运行结果为准，不能仅根据工作流文件已生成来判断。

## 已知限制

- 字典、Web 资源和运行时数据使用相对路径，启动时需要以 `build/` 作为工作目录。
- 当前没有项目级自动化测试、benchmark 结果或覆盖率结果，README 不据此作性能结论。
- 当前内存统计实现只在 Windows 分支读取进程内存；其他平台返回 `0`。
- HTTP 服务默认绑定所有网卡且没有认证层，适合本地或受控网络环境使用。
- 运行过程中会在 `data/` 和复制后的字典目录中产生状态文件；这些运行时文件不属于源码依赖。

## License

本项目采用 [MIT License](LICENSE)。第三方依赖各自保留其上游许可证。
