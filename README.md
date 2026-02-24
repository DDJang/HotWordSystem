# 🔥 HotWordSystem - 高性能实时热词分析系统

<p align="center">
    <img src="https://img.shields.io/badge/C%2B%2B-20-blue.svg" alt="C++20">
    <img src="https://img.shields.io/badge/CMake-3.10%2B-yellow.svg" alt="CMake">
    <img src="https://img.shields.io/badge/License-MIT-green.svg" alt="License: MIT">
    <img src="https://img.shields.io/badge/Platform-Windows%20%7C%20Linux-orange.svg" alt="Platform">
</p>

**HotWordSystem** 是一个基于 C++20 构建的高性能实时文本流分析引擎。它能够高效地处理连续的文本输入，进行实时的中文分词、数据清洗，并利用精巧的**双层动态滑动窗口算法**来统计热词频率（Top-K）与分析热度趋势。

项目内置了一个轻量级 Web 服务器，提供一个采用**液态玻璃 (Glassmorphism)** 设计风格的现代化 **Web Dashboard**。用户可以通过该仪表盘进行实时图表监控、历史数据回放、动态调整过滤策略、执行系统压测，并支持使用 Python 脚本进行自动化的数据注入。

---

### 💻 技术栈 (Technology Stack)

| 领域       | 技术/库                                                 | 用途                               |
| :--------- | :------------------------------------------------------ | :--------------------------------- |
| **后端**   | C++20, STL, `<thread>`, `<atomic>`                      | 核心逻辑、并发控制                 |
| **Web服务** | `httplib.h`                                             | 轻量级 HTTP/HTTPS 服务器           |
| **数据交换** | `nlohmann/json`                                         | 前后端 JSON 序列化                 |
| **NLP核心** | `cppjieba`, `utfcpp`                                    | 中文分词、词性标注、UTF-8 清洗     |
| **前端**   | HTML5, CSS3, Vanilla JavaScript (ES6+)                  | 用户界面、交互逻辑                 |
| **可视化** | `ECharts.js`                                            | 实时热词榜、历史数据图表           |
| **构建**   | `CMake`                                                 | 跨平台项目构建与资源管理           |

---

## ✨ 核心特性 & 设计理念 (Core Features & Design Philosophy)

1.  **双层滑动窗口架构 (Dual-Layer Sliding Window)**
    *   **物理存储层 (`storage`)**: 基于 `std::map`，保留一个远大于当前窗口的、按时间排序的原始词元数据。这为系统提供了强大的数据回溯能力。
    *   **逻辑视图层 (`activeData` & `wordCounts`)**: 基于 `std::deque` 和 `std::unordered_map`，仅维护当前窗口内的活跃数据和词频统计，保证了窗口滑动的 O(1) 均摊复杂度和 Top-K 查询的高效性。
    *   **核心优势**: 这种分离设计实现了**无损动态调整窗口大小**。当用户将窗口从 10 秒切换到 1 小时，系统无需重读日志，而是从物理层快速重建逻辑视图，提供了卓越的交互体验。

2.  **高性能与并发安全**
    *   **高效的锁管理**: 系统采用全局锁来保证复杂操作的原子性，但通过**最小化锁持有时间**（例如，在文本处理中采用“读时复制”策略）来降低锁竞争，在保证数据绝对一致性的同时，实现了高并发处理能力。
    *   **内存硬顶限制 (Hard Cap)**: 内置 `MAX_STORAGE_ENTRIES` 机制，当内存中的数据条目达到预设上限时，会强制淘汰最老的数据，并**通过前端UI告警**，有效防止极端数据量下发生 OOM (Out of Memory)。
    *   **多线程架构**: Web 服务与核心计算逻辑在不同线程运行，确保用户 UI 的流畅性不受数据处理后台任务的影响。

3.  **现代化前端与动态配置**
    *   **“液态玻璃”美学**: 前端 UI 采用现代化的 Glassmorphism 风格，结合动态渐变背景，提供了优雅的视觉体验。
    *   **配置热更新**: 无需重启服务，用户可在 Web 界面上**实时动态调整**敏感词库、词性过滤策略。所有配置的修改都会被持久化到文件中。
    *   **全栈监控仪表盘**: ECharts 实时大屏，不仅展示热词榜单与趋势，还通过弹窗实时显示后端的 QPS、内存占用、运行时间等核心性能指标。

4.  **健壮性设计**
    *   **完善的输入校验**: 对所有来自前端的数值和时间格式输入进行严格校验，防止越界或格式错误。
    *   **优雅停机 (Graceful Shutdown)**: 支持通过 `SHUTDOWN` 指令安全地停止服务器进程。
    *   **数据持久化与回放**: 所有处理过的数据都会被记录到日志中，支持基于任意时间范围的历史 Top-K 查询和报告生成。

5.  **多语言交互支持**
    *   提供了一套清晰的 HTTP API 接口，方便与其他系统集成。
    *   附带一个开箱即用的 **Python 客户端脚本 (`client.py`)**，用于批量数据投递和自动化测试。

---

## 📂 项目目录结构

项目遵循了清晰、模块化的目录组织方式：
```text
HotWordSystem/
├── CMakeLists.txt          # 项目构建脚本
├── include/                # C++ 头文件 (核心逻辑)
│   ├── APIServer.h         # Web 服务器 (支持静态文件服务)
│   ├── CommandExecutor.h   # 指令解析与分发
│   ├── GlobalUtils.h       # 全局通用工具 (时间转换、安全类型转换等)
│   ├── PerformanceMonitor.h# 性能监控模块
│   ├── PersistenceManager.h# 持久化与日志管理
│   ├── SlidingWindow.h     # 滑动窗口核心算法与数据结构
│   ├── SystemContext.h     # 全局上下文，管理模块实例与全局状态
│   └── TextProcessor.h     # 分词、过滤与文本清洗引擎
├── src/
│   ├── SystemContext.cpp   # 全局上下文，管理模块实例与全局状态
│   └── main.cpp            # 程序入口与线程管理
├── web/                    # 前端资源目录 (HTML/CSS/JS 分离)
│   ├── dashboard.html      # 主页面结构
│   ├── style.css           # 样式表 (Glassmorphism 风格)
│   └── script.js           # 前端交互逻辑
├── test/                   # 单元测试
│   ├── unittest/           # 单元测试代码
│   └── testdata/           # 测试用例数据
├── scripts/                # 辅助脚本
│   ├── client.py           # Python 数据投递客户端
│   └── test_data.txt       # 默认测试文本数据
└── third_party/            # 第三方依赖库 (cppjieba, httplib, json 等)
```

---

## 🛠️ 构建指南 (Build Instructions)

### 前置依赖
*   **C++ 编译器**: 支持 C++20 标准。
*   **CMake**: 版本 3.10 或更高。
*   **Python 3** (可选): 用于运行 `scripts/client.py` 脚本。

### 编译步骤

1.  **克隆项目**
    ```bash
    git clone --recurse-submodules https://github.com/DDJang/HotWordSystem.git
    cd HotWordSystem
    ```

2.  **创建并进入构建目录**
    ```bash
    mkdir build
    cd build
    ```

3.  **运行 CMake 配置**
    ```bash
    cmake ..
    ```
    *注: CMake 会自动处理依赖，并将 `dict/`, `web/`, `test/testdata/` 等资源复制到构建目录中。*

4.  **编译项目**
    *   **Linux / macOS**:
        ```bash
        make -j$(nproc)
        ```
    *   **Windows (MSVC)**:
        ```bash
        cmake --build . --config Release
        ```

---

## 🚀 快速启动与使用 (Quick Start & Usage)

### 1. 启动服务器
在 `build` 目录下，运行生成的可执行文件：

*   **Windows**: `.\Release\HotWordSystem.exe`
*   **Linux / macOS**: `./HotWordSystem`

当控制台显示以下信息时，表示服务器已成功启动：
```text
[Web] GUI Server running at http://localhost:8080
[Ready] System Online. Use Web UI or Console.
```

### 2. 访问可视化控制台
打开您的浏览器并访问：**[http://localhost:8080](http://localhost:8080)**

---

### 3. 数据投递方式

#### 方式 A: 使用 Python 脚本 (推荐)
项目提供了 `scripts/client.py`，可以方便地将文本文件内容逐行发送给服务器。

1.  确保服务器正在运行。
2.  打开一个新的终端窗口。
3.  运行脚本：
    ```bash
    # 安装依赖 (仅需一次)
    pip install requests

    # 从项目根目录运行脚本
    python scripts/client.py
    ```
    *脚本默认读取 `scripts/test_data.txt`。您可以编辑该文件，或修改脚本中的配置来调整发送速率或目标服务器地址。*

#### 方式 B: Web UI 数据模拟
在网页控制台的 **“📝 数据模拟”** 面板中，您可以：
*   在文本框中直接输入单行或多行数据/指令，然后点击“发送数据”。
*   点击“上传文件”按钮，选择一个本地的 `.txt` 文件进行批量投递。

#### 方式 C: 内置压力测试
在网页控制台的 **“⚡ 压力测试”** 面板中，输入希望注入的数据量，然后点击“启动测试”。系统将自动生成随机数据进行高并发测试。

#### 方式 D: 直接调用 HTTP API
您可以使用 `curl` 或任何 HTTP 工具直接与 API 交互：
```bash
curl -X POST http://localhost:8080/api/command -H "Content-Type: application/json" -d '{"cmd": "华为发布了新手机"}'
```

---

## ⚙️ 核心指令说明

系统支持纯文本数据与控制指令两种输入。为了获得最佳体验，请使用 Web UI 操作；直接输入指令可能无法触发前端相应的交互提示。

| 指令格式                                                    | 说明                                                         |
| :---------------------------------------------------------- | :----------------------------------------------------------- |
| `[12:00:00] 文本内容`                                       | 注入一条带有指定时间戳的数据                               |
| `[ACTION] SET_WINDOW S=600`                                 | 将实时滑动窗口的大小设置为 600 秒                            |
| `[ACTION] SET_RETENTION R=3600`                             | 将物理存储层的最大数据保留时间设置为 3600 秒                 |
| `[ACTION] QUERY K=5`                                        | 在控制台查询当前窗口的 Top-5 热词                            |
| `[ACTION] TREND K=5`                                        | 在控制台查询当前窗口的 Top-5 趋势词                          |
| `[ACTION] STATS`                                            | 在控制台打印当前性能指标                                     |
| `[ACTION] BENCHMARK N=10000`                                | 启动一轮包含 10000 条随机数据的高并发压力测试                |
| `[ACTION] HISTORY START=00:00:00 END=01:00:00 STEP=60`      | 生成一份从 0点 到 1点，以 60秒 为步长的历史报告 `report_output.txt` |
| `[ACTION] RESET`                                            | 清空内存中的所有数据、磁盘日志文件并重置系统时间戳 |
| `[ACTION] SHUTDOWN`                                         | 安全地终止服务器进程                              |

---

## 🧪 运行测试 (Testing)

项目包含一套单元测试，用于验证核心模块的正确性。

1.  确保项目已成功编译。
2.  在 `build` 目录下运行测试程序：
    *   **Windows**: `.\Release\RunTests.exe`
    *   **Linux / macOS**: `./RunTests`
3.  测试程序将自动执行。如果所有测试用例均通过，您将看到成功信息。

---

## ⚠️ 注意事项 & 行为解释

*   **资源路径**: 程序启动时依赖 `dict/` 和 `web/` 目录。`CMake` 已配置在构建时自动将它们复制到输出目录，请确保它们存在。
*   **内存使用**: 默认配置下，为防止 OOM，系统限制最大物理存储条目为 50 万条。此上限可在 `SlidingWindow.h` 的 `MAX_STORAGE_ENTRIES` 常量中修改。
*   **时间戳处理**: 若输入的文本行**不包含** `[HH:MM:SS]` 格式的时间戳，系统会**自动将上次记录的时间戳加一秒**作为该行数据的时间戳。

