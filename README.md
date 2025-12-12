# 🔥 HotWordSystem - 高性能实时热词分析系统

**HotWordSystem** 是一个基于 C++17 开发的高性能实时文本流分析引擎。它能够接收连续的文本输入，进行实时分词、清洗，并利用**动态滑动窗口算法**统计热词频率（Top-K）与趋势变化。

该项目内置了一个轻量级 Web 服务器，提供现代化的 **Web Dashboard**（HTML/CSS/JS 分离架构），支持实时图表展示、历史回放、动态配置、系统压测以及 Python 脚本自动化数据注入。

---

## ✨ 项目核心亮点 (Special Features)

1.  **双层滑动窗口架构 (Dual-Layer Sliding Window)**
    *   **物理存储层 (Storage)**：保留较长周期的原始数据，支持数据回溯。
    *   **逻辑视图层 (Logical View)**：仅统计当前窗口内的热度，支持**无损动态调整窗口大小**（例如从 10秒 瞬间切换到 1小时）。

2.  **高性能与并发安全**
    *   核心统计模块采用细粒度锁，确保高并发下的数据一致性。
    *   引入**内存硬顶限制 (Hard Cap)**，防止极端数据量下发生 OOM。
    *   多线程架构：Web 服务与计算逻辑分离。

3.  **现代化前端与动态配置**
    *   **前后端分离资源**：标准化的 `web/` 目录结构 (HTML/CSS/JS)。
    *   **热更新**：无需重启即可动态调整敏感词库、词性过滤策略（支持 iOS 风格开关控件）。
    *   **全栈监控**：ECharts 实时大屏，展示 QPS、内存占用、热词榜单及趋势分析。

4.  **健壮性设计**
    *   完善的输入校验（防越界、防注入）。
    *   支持**优雅停机 (Graceful Shutdown)** 和 **数据持久化/回放**。

5.  **多语言交互支持**
    *   提供 HTTP API 接口。
    *   提供 **Python 客户端脚本**，方便进行批量数据投递或自动化测试。

---

## 📂 项目目录结构

```text
HotWordSystem/
├── CMakeLists.txt          # 构建脚本
├── include/                # C++ 头文件 (核心逻辑)
│   ├── APIServer.h         # Web 服务器 (支持静态文件服务)
│   ├── CommandExecutor.h   # 指令解析
│   ├── GlobalUtils.h       # 通用工具
│   ├── PerformanceMonitor.h# 性能监控
│   ├── PersistenceManager.h# 持久化与日志
│   ├── SlidingWindow.h     # 滑动窗口核心算法
│   ├── SystemContext.h     # 全局上下文
│   └── TextProcessor.h     # 分词与文本清洗
├── src/
│   └── main.cpp            # 程序入口
├── web/                    # 前端资源目录
│   ├── dashboard.html      # 主页面结构
│   ├── style.css           # 样式表
│   └── script.js           # 前端逻辑
├── test/                   # 单元测试
│   ├── unittest/           # 测试代码
│   └── testdata/           # 测试用例数据
├── scripts/                # 辅助脚本
│   ├── client.py           # Python 数据投递客户端
│   └── test_data.txt       # 测试文本数据
└── third_party/            # 第三方依赖库
```

---

## 🛠️ 构建指南 (Build Instructions)

### 前置依赖
*   **C++ 编译器**: C++17 标准 (GCC 8+, Clang 6+, MSVC 2019+)。
*   **CMake**: 3.10+。
*   **Python 3** (可选，用于运行脚本)。

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
    *(CMake 会自动将 `dict/`, `web/`, `test/testdata/` 等资源复制到构建目录)*

4.  **编译项目**
    *   **Linux/Mac**: `make -j4`
    *   **Windows**: `cmake --build . --config Release`

---

## 🚀 运行与使用 (Usage)

### 1. 启动服务器
在 `build` 目录下运行生成的可执行文件：

*   **Windows**: `.\HotWordSystem.exe`
*   **Linux/Mac**: `./HotWordSystem`

控制台显示如下即启动成功：
```text
[Web] GUI Server running at http://localhost:8080
[Ready] System Online. Use Web UI or Console.
```

### 2. 访问可视化控制台
打开浏览器访问：**[http://localhost:8080](http://localhost:8080)**

---

### 3. 数据投递方式

#### 方式 A：使用 Python 脚本 (推荐用于批量测试)
项目提供了 `client.py`，可以将文本文件中的内容按行发送给服务器。

1.  确保服务器正在运行。
2.  打开新终端，进入 `scripts/` 目录（或在根目录操作）。
3.  运行脚本：
    ```bash
    # 安装依赖 (仅需一次)
    pip install requests

    # 运行脚本 (假设服务器在 localhost:8080)
    python scripts/client.py
    ```
    *注意：脚本默认会读取同目录下的 `test_data.txt` 并发送。您可以修改脚本中的配置来调整发送速率或目标地址。*

#### 方式 B：Web UI 模拟
在网页控制台的 **“📝 数据模拟”** 输入框中输入文本，点击发送。

#### 方式 C：内置压测
在网页控制台点击 **“⚡ 启动压力测试”**，系统将自动生成随机数据进行高并发测试。

#### 方式 D：HTTP API
```bash
curl -X POST http://localhost:8080/api/command -d '{"cmd": "华为发布了新手机"}'
```

---

## ⚙️ 核心指令说明

除了纯文本数据，系统支持以下控制指令（支持在 CLI、Web 输入框或 Python 脚本中发送，下面的数字均可**自定义**）：

| 指令格式 | 说明 |
| :--- | :--- |
| `[12:00:00] 文本内容` | 注入指定时间戳的数据 |
| `[ACTION] SET_WINDOW S=600` | 将滑动窗口大小设置为 600 秒 |
| `[ACTION] SET_RETENTION R=3600` | 将存储的时间长度设置为 3600 秒 |
| `[ACTION] QUERY K=5` | 查询 Top-5 热词 |
| `[ACTION] TREND K=5` | 查询 Trend |
| `[ACTION] STATS` | 查询性能 |
| `[ACTION] BENCHMARK N=1000` | 启动 1000 条数据的自动压测 |
| `[ACTION] HISTORY START=.. END=..` | 生成历史报告 |
| `[ACTION] RESET` | **清空**所有数据、日志并重置时间戳 |
| `[ACTION] SHUTDOWN` | 安全终止服务器进程 |




---

## 🧪 运行测试 (Testing)

项目包含完善的单元测试，涵盖了工具类、算法逻辑、持久化以及**基于文件的回放测试**。

1.  进入 `build` 目录。
2.  运行测试程序：
    *   **Windows**: `.\RunTests.exe`
    *   **Linux/Mac**: `./RunTests`
3.  测试程序会自动读取 `testdata/` 中的样本文件进行验证。如果输出 `🎉 ALL TESTS PASSED SUCCESSFULLY!`，说明系统功能正常。

---

## ⚠️ 注意事项

*   **字典路径**: 程序启动时必须能找到 `dict/` 目录，CMake 会自动处理。
*   **内存使用**: 默认配置下，为了防止 OOM，系统限制最大存储条目为 50万条。可通过 `SlidingWindow.h` 修改 `MAX_STORAGE_ENTRIES`。
*   **文本数据**: 若输入的文本内容未指定时间戳，则默认此文本内容时间为 *当前时间 + 1* 秒

---

**Author**: DDJang
**License**: MIT