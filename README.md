# 🔥 HotWordSystem - 高性能实时热词分析系统

**HotWordSystem** 是一个基于 C++17 开发的高性能实时文本流分析引擎。它能够接收连续的文本输入，进行实时分词、清洗，并利用**动态滑动窗口算法**统计热词频率（Top-K）与趋势变化。

该项目内置了一个轻量级 Web 服务器，提供可视化的 **Dashboard**，支持实时图表展示、历史回放、动态配置以及系统压测。

---

## ✨ 项目核心亮点 (Special Features)

本项目并非简单的计数器，其底层设计包含多个特殊优化：

1.  **双层滑动窗口架构 (Dual-Layer Sliding Window)**
    *   **物理存储层 (Storage)**：保留较长周期的原始数据（如 1 小时），防止数据丢失。
    *   **逻辑视图层 (Logical View)**：仅统计当前窗口（如 10 分钟）内的热度。
    *   **优势**：支持**动态调整窗口大小**（例如从 10秒 瞬间切换到 1小时），系统能立即利用物理层数据重建视图，实现无损的“时间伸缩”。

2.  **高性能与并发安全**
    *   核心统计模块采用细粒度锁与原子操作，确保高并发下的数据一致性。
    *   引入**内存硬顶限制 (Hard Cap)**，防止在极端数据量下发生 OOM (内存溢出)。
    *   支持多线程 Web 服务与主业务逻辑分离。

3.  **动态热更新配置**
    *   无需重启即可动态调整**敏感词库**。
    *   支持动态切换**词性过滤策略**（如仅看名词、动词，或保留所有）。
    *   所有配置通过 Web 界面即时生效。

4.  **全栈可视化监控**
    *   内置 **ECharts** 前端大屏，实时刷新 QPS、内存占用和热词榜单。
    *   支持**历史数据回放**功能，可指定任意过去的时间段生成分析快照。

5.  **健壮性设计**
    *   完善的输入校验（防越界、防注入）。
    *   支持优雅停机（Graceful Shutdown）。

---

## 🛠️ 构建指南 (Build Instructions)

### 前置依赖
*   **C++ 编译器**: 支持 C++17 标准 (GCC 8+, Clang 6+, MSVC 2019+)。
*   **CMake**: 版本 3.10 或以上。
*   **操作系统**: Windows / Linux / macOS。

### 第三方库 (已包含在 `third_party/` 中)
*   `cppjieba`: 中文分词库。
*   `httplib`: 轻量级 HTTP 服务器。
*   `nlohmann/json`: JSON 解析库。
*   `utfcpp`: UTF-8 编码处理。

### 编译步骤

1.  **克隆项目**
    ```bash
    git clone https://github.com/your-repo/HotWordSystem.git
    cd HotWordSystem
    ```

2.  **创建构建目录**
    ```bash
    mkdir build
    cd build
    ```

3.  **运行 CMake 配置**
    ```bash
    cmake ..
    ```

4.  **编译项目**
    *   **Linux/Mac**:
        ```bash
        make -j4
        ```
    *   **Windows**:
        ```bash
        cmake --build . --config Release
        ```

5.  **文件检查**
    编译完成后，`build` 目录下会自动复制 `dict/` 文件夹（分词字典）和 `dashboard.html`（前端页面）。确保它们存在。

---

## 🚀 运行与使用 (Usage)

### 1. 启动系统
在 `build` 目录下运行生成的可执行文件：

*   **Windows**: `.\Release\HotWordSystem.exe` (或 `.\HotWordSystem.exe`)
*   **Linux/Mac**: `./HotWordSystem`

启动后，控制台将显示：
```text
[Web] GUI Server running at http://localhost:8080
[Ready] System Online. Use Web UI or Console.
```

### 2. 访问控制台
打开浏览器访问：**[http://localhost:8080](http://localhost:8080)**

您将看到可视化控制台，包含以下功能区：
*   **实时热词榜**：动态柱状图。
*   **控制台**：视图设置（Window/Top-K）、数据模拟、系统操作。
*   **过滤配置**：勾选保留的词性或管理敏感词。

### 3. 数据交互方式

#### 方式 A：通过 Web UI (推荐)
*   在“数据模拟”输入框中输入文本，点击发送。
*   使用“系统操作”中的“启动压力测试”来模拟海量数据。

#### 方式 B：通过 HTTP API (编程接入)
您可以编写 Python/Java 脚本向系统推送数据：
```bash
# 推送数据
curl -X POST http://localhost:8080/api/command -d '{"cmd": "华为发布了新手机"}'

# 带有特定时间戳的数据
curl -X POST http://localhost:8080/api/command -d '{"cmd": "[12:00:01] 指定时间的文本"}'
```

#### 方式 C：通过标准输入 (CLI)
直接在运行程序的黑色终端窗口中输入文本并回车。

---

## ⚙️ 核心指令说明

系统支持通过文本指令控制（可以在 Web 模拟框或 CLI 输入）：

| 指令格式 | 说明 |
| :--- | :--- |
| `[12:00:00] 文本内容` | 注入指定时间戳的数据 |
| `[ACTION] SET_WINDOW S=600` | 将滑动窗口大小设置为 600秒 |
| `[ACTION] BENCHMARK N=1000` | 启动 1000 条数据的自动压测 |
| `[ACTION] RESET` | **清空**所有数据、日志并重置时间戳 |
| `[ACTION] SHUTDOWN` | 安全终止服务器进程 |
| `[ACTION] HISTORY START=.. END=..` | 生成历史报告 (通常由 UI 触发) |

---

## 📂 项目目录结构

```text
HotWordSystem/
├── CMakeLists.txt          # 构建脚本
├── include/                # 头文件 (核心逻辑)
│   ├── APIServer.h         # Web 服务器逻辑
│   ├── CommandExecutor.h   # 指令解析与执行
│   ├── GlobalUtils.h       # 通用工具 (时间、转换)
│   ├── PerformanceMonitor.h# 性能监控
│   ├── PersistenceManager.h# 持久化与日志
│   ├── SlidingWindow.h     # 滑动窗口算法 (核心)
│   ├── SystemContext.h     # 全局上下文管理
│   └── TextProcessor.h     # 分词与文本清洗
├── src/
│   └── main.cpp            # 程序入口
├── test/                   # 单元测试
│   └── unittest/
│       └── test_main.cpp   # 测试入口
├── dict/                   # Jieba 分词字典文件
├── third_party/            # 第三方依赖库
└── dashboard.html          # 前端可视化界面
```

---

## 🧪 运行测试

项目包含完整的单元测试，用于验证算法准确性。

1.  在 `build` 目录下。
2.  运行测试程序：
    *   Windows: `.\RunTests.exe`
    *   Linux/Mac: `./RunTests`
3.  如果输出 `🎉 ALL TESTS PASSED SUCCESSFULLY! 🎉`，说明系统功能正常。

---

## ⚠️ 注意事项

*   **字典路径**: 程序启动时必须能找到 `dict/` 目录。CMake 会自动处理，但如果手动移动了 `.exe`，请连同 `dict` 文件夹和 `dashboard.html` 一起移动。
*   **内存使用**: 默认配置下，为了防止 OOM，系统限制最大存储条目为 50万条。可通过 `SlidingWindow.h` 修改 `MAX_STORAGE_ENTRIES`。

---

**Author**: DDJang
**License**: MIT