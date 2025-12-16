var currentWindowSize = document.getElementById('winSize').value;
var currentRetentionSize = document.getElementById('retSize').value;
var feedbackTimer = null;
var isHistoryMode = false;
var wordToRemove = null; // 用于存储待删除的敏感词
var historyWindowText = "";
var benchmarkPollTimer = null; // 用于存放轮询定时器的ID

var myChart = echarts.init(document.getElementById('mainChart'));
myChart.setOption({
    tooltip: {
        trigger: 'axis',

        // 1. 背景色设为半透明白色
        backgroundColor: 'rgba(255, 255, 255, 0.6)',

        // 2. 边框颜色 (玻璃边缘的高光)
        borderColor: 'rgba(255, 255, 255, 0.8)',
        borderWidth: 1,

        // 3. 文字颜色改为深色
        textStyle: {
            color: '#1f2937', // 深灰色文字
            fontSize: 13,
            fontWeight: 600
        },

        // 4. 注入 CSS 实现磨砂玻璃 + 液态阴影效果
        extraCssText: `
        backdrop-filter: blur(12px);
        -webkit-backdrop-filter: blur(12px);
        box-shadow: 0 8px 32px 0 rgba(31, 38, 135, 0.15);
        border-radius: 12px;
    `
    },
    grid: { left: '3%', right: '5%', bottom: '13%', containLabel: true, top: '10%' },
    xAxis: {
        type: 'category',
        data: [],
        axisLabel: { color: '#6b7280' }, // -> 坐标文字颜色变柔和
        axisTick: { show: false }, // -> 隐藏刻度线
        axisLine: { lineStyle: { color: 'rgba(0,0,0,0.1)' } } // -> 轴线颜色变淡
    },
    yAxis: {
        type: 'value',
        axisLabel: { color: '#6b7280' },
        splitLine: { lineStyle: { color: 'rgba(0,0,0,0.05)', type: 'dashed' } } // -> 网格线变虚、变淡
    },
    series: [{
        type: 'bar',
        data: [],
        itemStyle: {
            // 【核心】定义柱体的渐变色
            color: new echarts.graphic.LinearGradient(0, 0, 0, 1, [
                { offset: 0, color: 'rgba(59, 130, 246, 1)' },   // 顶部：主题蓝色
                { offset: 1, color: 'rgba(37, 99, 235, 0.7)' }   // -> 底部：更深、半透明的纯蓝色
            ]),
            borderRadius: 10, // -> 圆角胶囊形状

            // 【核心】为柱体增加阴影，营造悬浮感
            shadowColor: 'rgba(0, 0, 0, 0.2)',
            shadowBlur: 10,
            shadowOffsetY: 5
        },
        // 【核心】鼠标悬浮时的辉光效果
        emphasis: {
            itemStyle: {
                // 悬浮时，使用更亮的蓝色作为辉光
                shadowColor: 'rgba(59, 130, 246, 0.8)',
                shadowBlur: 20
            }
        },
        label: {
            show: true,
            position: 'top',
            color: '#374151', // -> 标签文字颜色
            fontWeight: 'bold'
        }
    }]
});

window.addEventListener('resize', function () {
    myChart.resize();
});

var currentBackendTimestamp = "00:00:00";
var isInitialLoad = true;


// 最大同时显示的弹窗数量
const MAX_VISIBLE_NOTIFICATIONS = 1;

function showNotification(title, message, type = 'warning', duration = 5000) {
    const container = document.getElementById('notification-container');

    // === 1. 队列控制逻辑 (挤掉旧的) ===
    const activeToasts = container.querySelectorAll('.notification-toast:not(.hiding)');

    if (activeToasts.length >= MAX_VISIBLE_NOTIFICATIONS) {
        const toCloseCount = activeToasts.length - MAX_VISIBLE_NOTIFICATIONS + 1;
        for (let i = 0; i < toCloseCount; i++) {
            if (activeToasts[i] && typeof activeToasts[i].close === 'function') {
                activeToasts[i].close();
            }
        }
    }

    // === 2. 创建弹窗 ===
    const toast = document.createElement('div');
    toast.className = `notification-toast is-${type}`;

    // 定义关闭逻辑
    const closeToast = () => {
        if (toast.classList.contains('hiding')) return;
        toast.classList.add('hiding');
        toast.addEventListener('animationend', () => {
            if (toast.parentElement) toast.remove();
        });
    };

    // 挂载关闭方法
    toast.close = closeToast;

    // 点击弹窗时关闭
    toast.onclick = closeToast;
    toast.style.cursor = 'pointer'; // 让鼠标变成手型，提示可点击关闭

    toast.innerHTML = `
        <h4>${title}</h4>
        <p>${message}</p>
    `;

    container.appendChild(toast);

    // === 3. 自动关闭定时器 ===
    if (duration > 0) {
        setTimeout(() => {
            if (document.body.contains(toast) && !toast.classList.contains('hiding')) {
                closeToast();
            }
        }, duration);
    }
}


function fetchAndUpdateRealtimeChart() {
    let k = document.getElementById('displayK').value || 10;
    const overlay = document.getElementById('chart-state-overlay');
    const overlayText = overlay.querySelector('p');

    // 在 fetch 开始前，如果是首次加载，显示“加载中”
    if (isInitialLoad) {
        overlay.classList.remove('no-data');
        overlay.classList.add('show');
        overlayText.textContent = '正在连接服务器...';
    }

    fetch('/api/data?k=' + k)
        .then(res => res.json())
        .then(data => {
            // 1. 处理系统关闭情况
            if (data.shutdown) {
                clearInterval(mainInterval);
                document.body.innerHTML = "<div style='display:flex;justify-content:center;align-items:center;height:100vh;flex-direction:column;color:#555;'><h1>🚫 系统已关闭</h1><p>连接已断开，请手动重启服务。</p></div>";
                return;
            }

            // 2. 处理通知
            if (data.capacity_limit_evicted) {
                showNotification('内存警告', '系统内存占用已达上限！为保证稳定性，部分最老的数据已被强制清除。', 'danger', 10000);
            } else if (data.time_limit_evicted) {
                showNotification('数据清理提示', '部分历史数据因超出设置的最大保留时间，已被常规清除。', 'warning');
            }

            // 3. 【核心修复】安全地更新全局状态
            // 只有当服务器返回有效值时，才更新用于 Info 弹窗的全局变量
            if (data.window_sec !== undefined) {
                currentWindowSize = data.window_sec;
                // 【关键】只有在首次加载时，才去修改输入框的值！
                // 之后无论服务器返回什么，都不要动输入框，防止打断用户打字
                if (isInitialLoad) {
                    document.getElementById('winSize').value = data.window_sec;
                }
            }

            if (data.retention_sec !== undefined) {
                currentRetentionSize = data.retention_sec;
                // 【关键】同上，只在首次加载时修改输入框
                if (isInitialLoad) {
                    document.getElementById('retSize').value = data.retention_sec;
                }
            }

            // 4. 更新图表
            if (!isHistoryMode) {
                const seriesData = data.values.map((value, index) => ({
                    value: value,
                    id: data.categories[index]
                }));

                myChart.setOption({
                    xAxis: { data: data.categories },
                    series: [{ data: seriesData }]
                });
            }

            // 5. 更新文本信息
            currentBackendTimestamp = data.current_ts;
            if (!isHistoryMode) {
                document.getElementById('windowInfo').innerText =
                    `Window: [${data.window_start} ~ ${data.window_end}] | Now: ${data.current_ts}`;
            } else {
                document.getElementById('windowInfo').innerText =
                    `${historyWindowText} | Now: ${data.current_ts}`;
            }

            // 6. 关闭首次加载标志
            if (isInitialLoad) {
                log("✅ System Ready & Connected", data.current_ts);
                isInitialLoad = false;
            }

            // 7. 根据返回的数据决定显示内容
            if (data.categories && data.categories.length > 0) {
                // 如果有数据，隐藏遮罩层
                overlay.classList.remove('show');

                if (!isHistoryMode) {
                    const seriesData = data.values.map((value, index) => ({
                        value: value,
                        id: data.categories[index]
                    }));

                    myChart.setOption({
                        xAxis: { data: data.categories },
                        series: [{ data: seriesData }]
                    });
                }
            } else {
                // 如果没有数据，显示“无数据”状态
                overlay.classList.add('show', 'no-data');
                overlayText.innerHTML = '📂<br>暂无数据';
                // 清空图表
                myChart.setOption({
                    xAxis: { data: [] },
                    series: [{ data: [] }]
                });
            }
        }).catch(error => {
            // 【新增】处理网络错误
            console.error("Fetch error:", error);
            overlay.classList.add('show', 'no-data');
            overlayText.innerHTML = '❌<br>连接失败，请检查服务是否运行。';
        });
}

var mainInterval = setInterval(() => {
    fetchAndUpdateRealtimeChart();
    fetch('/api/trends?k=10').then(r => r.json()).then(list => renderTrends(list));
}, 1000);


function validateNumber(id, min, max, name) {
    const el = document.getElementById(id);
    const val = parseInt(el.value);

    // 检查1：是否为有效数字
    if (isNaN(val)) {
        // 原来的 alert() 已被替换
        showNotification(
            '输入无效',
            `[${name}] 请输入一个有效的数字。`,
            'danger', // 红色危险弹窗
            5000      // 显示5秒
        );
        return null; // 保持原有逻辑，中断操作
    }

    // 检查2：是否在允许的范围内
    if (val < min || val > max) {
        // 原来的 alert() 已被替换
        showNotification(
            '数值越界',
            `[${name}] 的值必须在 ${min} 到 ${max} 之间。`,
            'danger',
            5000
        );
        return null;
    }

    return val;
}


function validateTime(id) {
    const el = document.getElementById(id);
    const val = el.value.trim();
    const regex = /^\d{1,2}:\d{1,2}:\d{1,2}$/;

    if (!regex.test(val)) {
        // 原来的 alert() 已被替换
        showNotification(
            '格式错误',
            `您输入的时间“${val}”格式不正确，请使用 HH:MM:SS 格式。`,
            'danger',
            5000
        );
        return null;
    }

    return val;
}


// 切换词性过滤启用状态
function togglePosFilter(el, suppressUpdate = false) {
    const isEnabled = el.checked; // 勾选 = 启用过滤
    const configArea = document.getElementById('posConfigArea');

    // UI 视觉反馈
    if (isEnabled) {
        configArea.style.opacity = "1";
        configArea.style.pointerEvents = "auto";
    } else {
        configArea.style.opacity = "0.4";
        configArea.style.pointerEvents = "none";
    }

    if (!suppressUpdate) {
        updateConfig();
    }
}

// 切换敏感词过滤启用状态
function toggleSensitiveFilter(el, suppressUpdate = false) {
    const isEnabled = el.checked; // 勾选 = 启用过滤
    const configArea = document.getElementById('sensitiveConfigArea');

    if (isEnabled) {
        configArea.style.opacity = "1";
        configArea.style.pointerEvents = "auto";
    } else {
        configArea.style.opacity = "0.4";
        configArea.style.pointerEvents = "none";
    }

    if (!suppressUpdate) {
        updateConfig();
    }
}


function updateConfig() {
    // 【关键】后台是 allow_all (是否保留所有)
    // 我们的开关是 enable_filter (是否启用过滤)
    // 所以：allow_all = !enable_filter

    const enablePosFilter = document.getElementById('cb_filter_pos_enable').checked;
    const enableSensitiveFilter = document.getElementById('cb_filter_sensitive_enable').checked;

    const checks = document.querySelectorAll('.pos-item:checked');
    let tags = [];
    checks.forEach(c => tags.push(c.value));

    apiPost('/api/config', {
        allow_all: !enablePosFilter,          // 取反
        allow_sensitive: !enableSensitiveFilter, // 取反 (allow_sensitive=true 表示保留所有敏感词，即不过滤)
        tags: tags
    });

    showNotification('配置已更新', '新的过滤规则已应用', 'success', 2000);
}


function loadConfigState() {
    fetch('/api/config').then(r => r.json()).then(d => {
        // 加载敏感词列表
        const div = document.getElementById('sensitiveList'); div.innerHTML = "";
        if (d.sensitive_words) {
            d.sensitive_words.forEach(w => {
                const s = document.createElement('span');
                s.className = "tag-chip";
                s.style.background = "#fee2e2";
                s.style.color = "#b91c1c";
                s.style.borderColor = "#fecaca";
                s.innerHTML = `${w} ×`;
                s.onclick = () => removeSensitive(w);
                div.appendChild(s);
            });
        }

        // --- 词性过滤 ---
        // 后台 d.allow_all 为 true -> 代表保留所有 -> 开关应该【不勾选】
        const cbPos = document.getElementById('cb_filter_pos_enable');
        cbPos.checked = !d.allow_all; // 取反
        togglePosFilter(cbPos, true); // 初始化视觉状态

        // --- 敏感词过滤 ---
        // 后台 d.allow_sensitive 为 true -> 代表保留所有 -> 开关应该【不勾选】
        const cbSens = document.getElementById('cb_filter_sensitive_enable');
        cbSens.checked = !d.allow_sensitive; // 取反
        toggleSensitiveFilter(cbSens, true); // 初始化视觉状态

        // 加载选中的标签
        if (d.tags) {
            const checkboxes = document.querySelectorAll('.pos-item');
            checkboxes.forEach(cb => cb.checked = false);
            d.tags.forEach(tag => {
                checkboxes.forEach(cb => {
                    if (cb.value === tag) cb.checked = true;
                });
            });
        }
    });
}

loadConfigState();


function renderTrends(list) {
    const tbody = document.getElementById('trendBody');
    const trendOverlay = document.getElementById('trend-state-overlay'); // 获取新的遮罩层
    const threshold = parseFloat(document.getElementById('trendThreshold').value) || 0;

    // 【核心逻辑】根据列表是否为空来显示/隐藏遮罩层
    if (!list || list.length === 0) {
        trendOverlay.classList.add('show');
        tbody.innerHTML = ""; // 确保表格为空
        return; // 提前结束函数
    } else {
        trendOverlay.classList.remove('show');
    }

    tbody.innerHTML = ""; // 清空旧数据
    let rank = 1;

    const positiveScores = list.map(item => item.score).filter(score => score > 0);
    const maxScore = positiveScores.length > 0 ? Math.max(...positiveScores) : 1;

    list.forEach(item => {
        if (Math.abs(item.score) < threshold) return;
        const tr = document.createElement('tr');
        if (item.score > 0) {
            const hotness = item.score / maxScore;
            const alpha = 0.5 + (hotness * 0.5);
            const colorStyle = `style="color: rgba(220, 38, 38, ${alpha});"`;
            let statusEmoji = '🔥';
            if (hotness >= 0.8) statusEmoji = '🔥🔥🔥';
            else if (hotness >= 0.4) statusEmoji = '🔥🔥';
            if (item.score === maxScore && maxScore > threshold) statusEmoji = '🌋';
            tr.innerHTML = `<td>${rank++}</td><td style="font-weight:bold;">${item.word}</td><td class="trend-up" ${colorStyle}>${item.score.toFixed(2)}</td><td>${statusEmoji}</td>`;
        } else {
            tr.innerHTML = `<td>${rank++}</td><td style="font-weight:bold;">${item.word}</td><td class="trend-down">${item.score.toFixed(2)}</td><td>❄️</td>`;
        }
        tbody.appendChild(tr);
    });
}

function apiPost(url, data) {
    log("Sending...");
    // 【修改点1】在 fetch 前面加上 return，把承诺返回出去
    return fetch(url, { method: 'POST', headers: { 'Content-Type': 'application/json' }, body: JSON.stringify(data) })
        .then(r => r.json())
        .then(d => {
            const serverTime = d.timestamp || null;
            log("✅ " + (d.message || "OK"), serverTime);
            return d; // 【修改点2】把服务器的数据传给下一步
        })
        .catch(e => {
            log("❌ " + e);
            throw e; // 【修改点3】如果出错，抛出错误让调用者知道
        });
}
function log(msg, customTime = null) {
    const el = document.getElementById('log');
    const timeStr = customTime ? customTime : new Date().toLocaleTimeString();
    el.innerText = `[${timeStr}] ${msg}`;
}

function updateWindowSize() {
    const s = validateNumber('winSize', 1, 2592000, "Window Size");
    if (s !== null) {
        currentWindowSize = s;
        apiPost('/api/command', { cmd: `[ACTION] SET_WINDOW S=${s}` });
        showNotification('视图设置', `窗口大小已更新为 ${s} 秒`, 'success');
    }
}

function updateRetention() {
    // 1. 获取 Window 输入框的当前值
    const winSize = parseInt(document.getElementById('winSize').value) || 0;

    // 2. 获取 Storage 输入框本身，以读取其 min 和 max 属性
    const retSizeInput = document.getElementById('retSize');
    const retMinHardcoded = parseInt(retSizeInput.min); // Storage 的硬编码最小值 (e.g., 600)
    const retMaxHardcoded = parseInt(retSizeInput.max); // Storage 的硬编码最大值 (e.g., 2592000)

    // 3. 预检查：Window 的值是否已经超过了系统允许的最大值？
    if (winSize > retMaxHardcoded) {
        showNotification(
            '操作无效',
            `窗口大小 (${winSize}s) 不能超过系统最大存储上限 (${retMaxHardcoded}s)。`,
            'danger',
            6000 // 显示时间稍长，因为信息量大
        );
        return; // 终止函数，不继续执行
    }

    // 4. 计算用于验证的有效最小值 (effectiveMin)。
    //    它应该是 "Window" 的值和 "Storage" 硬编码最小值中，较大的那一个。
    const effectiveMin = Math.max(winSize, retMinHardcoded);

    // 5. 使用计算出的有效最小值进行验证
    const r = validateNumber('retSize', effectiveMin, retMaxHardcoded, "Retention");

    if (r !== null) {
        currentRetentionSize = r;
        apiPost('/api/command', { cmd: `[ACTION] SET_RETENTION R=${r}` });
        showNotification('存储设置', `数据保留时间已更新为 ${r} 秒`, 'success');
    }
}

function sendManualData() {
    const inputEl = document.getElementById('manualInput');
    const val = inputEl.value; // 获取原始值

    // 检查是否为空（去除首尾空格）
    if (!val || val.trim() === "") {
        showNotification('输入为空', '请输入模拟数据或控制指令后再发送。', 'warning');
        return;
    }

    // 发送数据
    apiPost('/api/command', { cmd: val });
    
    // 清空输入框
    inputEl.value = ""; 
    
    showNotification('数据发送', '模拟数据/指令已发送至服务器', 'success');

    // 检查是否为 SHUTDOWN 指令
    const shutdownRegex = /\[ACTION\]\s+SHUTDOWN/i;
    if (shutdownRegex.test(val)) {
        setTimeout(() => {
            document.body.innerHTML = "<div style='display:flex;justify-content:center;align-items:center;height:100vh;flex-direction:column;color:#555;'><h1>🚫 系统已关闭</h1><p>连接已断开，请手动重启服务。</p></div>";
        }, 1000);
    }
}

function addSensitive() {
    const inputEl = document.getElementById('newSensitive');
    const w = inputEl.value.trim(); // 获取值并去除首尾空格

    // 检查是否为空
    if (!w) {
        showNotification('输入为空', '请输入您想要屏蔽的敏感词。', 'warning');
        return;
    }

    // 检查长度是否超过10
    if (w.length > 10) {
        showNotification('格式错误', '敏感词长度不能超过 10 个字符。', 'danger');
        return;
    }

    // 发送请求
    apiPost('/api/config', { add_sensitive: w });
    
    // 清空输入框
    inputEl.value = ""; 
    
    // 刷新配置并显示成功提示
    setTimeout(loadConfigState, 500);
    showNotification('屏蔽词添加成功', `已将 "${w}" 加入屏蔽列表。`, 'success');
}
function removeSensitive(w) {
    // 1. 将待删除的词存到全局变量
    wordToRemove = w;

    // 2. 填充弹窗的动态内容
    const textElement = document.getElementById('removeSensitiveText');
    textElement.innerHTML = `您确定要从屏蔽列表中移除 “<span class="highlight-word">${w}</span>” 吗？`;

    // 3. 显示弹窗
    const modal = document.getElementById('removeSensitiveModal');
    modal.classList.add('show');
}


function viewHistory() {
    // 1. 数据验证 (保持不变)
    const s = validateTime('histStart');
    const e = validateTime('histEnd');
    if (!s || !e) return;

    const k = document.getElementById('displayK').value || 10;

    // 2. 立即更新 UI 和全局状态，提供即时反馈
    isHistoryMode = true; // 更新状态
    const toggleButton = document.getElementById('historyToggleButton');
    toggleButton.textContent = '返回实时'; // 改变文字
    toggleButton.classList.remove('purple');  // 变为默认蓝色
    toggleButton.classList.add('btn-pulse');  // 添加脉冲动画

    document.getElementById('history-mode-overlay').classList.add('show');
    historyWindowText = `MODE: HISTORY [${s} ~ ${e}]`;
    document.getElementById('windowInfo').innerText = `${historyWindowText} | Now: ${currentBackendTimestamp}`;
    log(`Loading History...`, currentBackendTimestamp);
    showNotification('历史回放', `已进入回放模式 [${s} ~ ${e}]，实时更新暂停`, 'warning', 4000);

    // 3. 发送网络请求，只在成功后更新图表数据
    fetch(`/api/history_view?start=${encodeURIComponent(s)}&end=${encodeURIComponent(e)}&k=${k}`)
        .then(r => r.json())
        .then(d => {
            // 这里只保留对图表数据的更新
            const historySeriesData = d.values.map((value, index) => ({
                value: value,
                id: d.categories[index]
            }));

            myChart.setOption({
                xAxis: { data: d.categories },
                series: [{
                    data: historySeriesData,
                    itemStyle: {
                        color: new echarts.graphic.LinearGradient(0, 0, 0, 1, [
                            { offset: 0, color: 'rgba(168, 85, 247, 1)' },
                            { offset: 1, color: 'rgba(139, 92, 246, 0.6)' }
                        ])
                    }
                }],
                grid: {
                    top: 80
                }
            });
        })
        .catch(error => {
            // [新增] 错误处理，让调试更方便
            console.error('Failed to fetch history data:', error);
            log("❌ Error loading history.", currentBackendTimestamp);
            showNotification('加载失败', '无法获取历史数据，请检查连接或时间范围。', 'danger');
        });
}


function backToRealtime() {
    // 1. 立即更新 UI 和全局状态
    isHistoryMode = false;
    const toggleButton = document.getElementById('historyToggleButton');
    toggleButton.textContent = '回放图表'; // 恢复文字
    toggleButton.classList.add('purple');     // 恢复紫色
    toggleButton.classList.remove('btn-pulse'); // 移除脉冲

    document.getElementById('history-mode-overlay').classList.remove('show');
    log("Back to Realtime.", currentBackendTimestamp);
    showNotification('实时监控', '已退出回放模式，恢复实时数据流', 'success');

    // 2. 恢复图表样式
    myChart.setOption({
        series: [{
            itemStyle: {
                color: new echarts.graphic.LinearGradient(0, 0, 0, 1, [
                    { offset: 0, color: 'rgba(59, 130, 246, 1)' },
                    { offset: 1, color: 'rgba(37, 99, 235, 0.7)' }
                ])
            }
        }],
        grid: { top: '10%' }
    });

    // 3. 获取并更新实时数据
    fetchAndUpdateRealtimeChart();
}

function showStats() {
    // 先获取数据
    fetch('/api/stats').then(r => r.json()).then(d => {
        document.getElementById('st_time').innerText = d.runtime.toFixed(2) + " s";
        document.getElementById('st_lines').innerText = d.lines;
        document.getElementById('st_words').innerText = d.words;
        document.getElementById('st_qps').innerText = d.qps.toFixed(2);
        document.getElementById('st_mem').innerText = d.memory.toFixed(2) + " MB";

        // 数据填充完毕后，添加 .show 类触发 CSS 动画
        const modal = document.getElementById('statsModal');
        modal.classList.add('show');
    });
}

function closeStats(e) {
    const modal = document.getElementById('statsModal');

    // 如果没有传事件对象(点击按钮)，或者点击的是遮罩层本身(背景)，则关闭
    if (!e || e.target === modal) {
        modal.classList.remove('show');
    }
}

function genReport() {
    const s = validateTime('histStart');
    const e = validateTime('histEnd');
    if (s && e) {
        apiPost('/api/command', { cmd: `[ACTION] HISTORY START=${s} END=${e} STEP=60` });
        showNotification('导出报告', '正在后台生成历史数据报告，请查看服务器日志', 'success');
    }
}





function resetSystem() {
    const modal = document.getElementById('resetModal');
    modal.classList.add('show');
}

function shutdownSystem() {
    const modal = document.getElementById('shutdownModal');
    modal.classList.add('show');
}

// 文件上传处理函数
function uploadFile(event) {
    const file = event.target.files[0];
    if (!file) {
        return;
    }

    log(`Reading file: ${file.name}...`);
    showNotification('文件处理', `正在读取文件: ${file.name}`, 'warning');

    const reader = new FileReader();
    reader.onload = function (e) {
        const content = e.target.result;
        log(`File read successfully, sending ${Math.round(content.length / 1024)} KB to server...`);
        apiPost('/api/command', { cmd: content });

        const sizeKB = Math.round(content.length / 1024);
        showNotification('上传成功', `已发送文件内容 (${sizeKB} KB) 到服务器`, 'success');
    };
    reader.onerror = function (e) {
        log(`❌ Error reading file: ${e.type}`);
    };
    reader.readAsText(file);

    // 清空 input 的值，这样用户可以重复上传同一个文件
    event.target.value = '';
}

/**
 * 点击信息图标时触发
 * 弹出模态框显示配置详情
 */
function showConfigInfo() {
    // 1. 获取当前输入框的值
    // 【核心修改-2】不再从输入框读取，而是从我们存储的全局变量读取
    const win = currentWindowSize;
    const ret = currentRetentionSize;
    const k = document.getElementById('displayK').value;
    const trend = document.getElementById('trendThreshold').value;

    // 2. 填充到模态框
    document.getElementById('cfg_win').innerText = win + " s";
    document.getElementById('cfg_ret').innerText = ret + " s";
    document.getElementById('cfg_k').innerText = k;
    document.getElementById('cfg_trend').innerText = trend;

    // 3. 显示模态框
    const modal = document.getElementById('configModal');
    modal.classList.add('show');
}

/**
 * 关闭配置详情模态框
 */
function closeConfigModal(e) {
    const modal = document.getElementById('configModal');
    // 如果没有传事件对象(点击按钮)，或者点击的是遮罩层本身(背景)，则关闭
    if (!e || e.target === modal) {
        modal.classList.remove('show');
    }
}

/**
 * 显示词性过滤的帮助信息
 */
function showPosInfo() {
    const isEnabled = document.getElementById('cb_filter_pos_enable').checked;
    const title = '🏷️ 词性过滤说明';
    let content = '';

    if (!isEnabled) {
        content = '<p>当前 <strong>未启用</strong> 词性过滤。</p><p>系统将保留并统计所有接收到的词汇，无论其词性如何。</p>';
    } else {
        content = '<p>当前 <strong>已启用</strong> 词性过滤。</p><p>只有 <span class="highlight-blue">蓝色高亮</span> 的标签所代表的词性才会被保留和统计，其他词性将被忽略。</p>';
    }
    showInfoModal(title, content);
}
function showSensitiveInfo() {
    const isEnabled = document.getElementById('cb_filter_sensitive_enable').checked;
    const title = '🛡️ 敏感词过滤说明';
    let content = '';

    if (!isEnabled) {
        content = '<p>当前 <strong>未启用</strong> 敏感词过滤。</p><p>所有词汇（包括已添加的敏感词）都将正常显示在榜单中。</p>';
    } else {
        content = '<p>当前 <strong>已启用</strong> 敏感词过滤。</p><p>您在下方列表中添加的所有词汇，都将被系统自动屏蔽，不会出现在热词统计中。</p>';
    }
    showInfoModal(title, content);
}


function closeShutdownModal(e) {
    const modal = document.getElementById('shutdownModal');
    if (!e || e.target === modal) {
        modal.classList.remove('show');
    }
}

function executeShutdown() {
    // 1. 立即关闭弹窗
    closeShutdownModal(null);

    // 2. 执行旧的关机指令逻辑
    apiPost('/api/command', { cmd: `[ACTION] SHUTDOWN` });
    showNotification('系统关闭', '正在断开连接并停止服务...', 'danger', 10000);
    setTimeout(() => {
        document.body.innerHTML = "<div style='display:flex;justify-content:center;align-items:center;height:100vh;flex-direction:column;color:#555;'><h1>🚫 系统已关闭</h1><p>连接已断开，请手动重启服务。</p></div>";
    }, 1000);
}



/**
 * 关闭清空数据确认弹窗
 */
function closeResetModal(e) {
    const modal = document.getElementById('resetModal');
    if (!e || e.target === modal) {
        modal.classList.remove('show');
    }
}
function executeReset() {
    closeResetModal(null);

    if (benchmarkPollTimer) {
        clearInterval(benchmarkPollTimer);
        benchmarkPollTimer = null;
        updateBenchmarkButtonUI(false);
    }

    document.getElementById('history-mode-overlay').classList.remove('show');
    log("Sending Reset...", "00:00:00");

    fetch('/api/command', {
        method: 'POST',
        headers: { 'Content-Type': 'application/json' },
        body: JSON.stringify({ cmd: `[ACTION] RESET` })
    })
        .then(r => r.json())
        .then(d => {
            isHistoryMode = false;
            myChart.setOption({
                xAxis: { data: [] },
                series: [{ data: [], itemStyle: { color: '#3b82f6' } }],
                grid: { top: '10%' }
            });
            document.getElementById('trendBody').innerHTML = "";
            document.getElementById('windowInfo').innerText = "Window: [Reset] | Now: 00:00:00";


            // 1. 获取正确的、合并后的新按钮
            const toggleButton = document.getElementById('historyToggleButton');

            // 2. 彻底重置按钮的所有状态，恢复到初始样貌
            toggleButton.textContent = '回放图表';
            toggleButton.classList.add('purple');
            toggleButton.classList.remove('btn-pulse');

            document.getElementById('history-mode-overlay').classList.remove('show');
            log("✅ System & Log Cleared.", "00:00:00");
            showNotification('系统重置', '内存数据及日志文件已全部清空', 'danger', 6000);
        })
        .catch(e => {
            log("❌ " + e);
            showNotification('重置失败', '连接服务器时发生错误', 'danger');
        });
}



/**
 * 打开通用信息弹窗
 * @param {string} title - 弹窗的标题
 * @param {string} htmlContent - 弹窗的HTML内容
 */
function showInfoModal(title, htmlContent) {
    document.getElementById('infoModalTitle').innerText = title;
    document.getElementById('infoModalContent').innerHTML = htmlContent;
    document.getElementById('infoModal').classList.add('show');
}
/**
 * 关闭通用信息弹窗
 */
function closeInfoModal(e) {
    const modal = document.getElementById('infoModal');
    if (!e || e.target === modal) {
        modal.classList.remove('show');
    }
}



/**
 * 【新增】关闭删除敏感词确认弹窗
 */
function closeRemoveSensitiveModal(e) {
    const modal = document.getElementById('removeSensitiveModal');
    if (!e || e.target === modal) {
        modal.classList.remove('show');
    }
}
function executeRemoveSensitive() {
    // 确保我们有一个有效的待删除词
    if (wordToRemove) {
        // 1. 执行旧的删除逻辑
        apiPost('/api/config', { remove_sensitive: wordToRemove });
        setTimeout(loadConfigState, 500);
        showNotification('屏蔽词已移除', `“${wordToRemove}” 已从屏蔽列表中删除。`, 'success');

        // 2. 清理工作
        wordToRemove = null; // 清空全局变量
    }

    // 3. 关闭弹窗
    closeRemoveSensitiveModal(null);
}


function toggleHistoryMode() {
    // isHistoryMode 是您已有的全局变量，我们用它来判断当前状态
    if (isHistoryMode) {
        // 如果当前是历史模式，就执行“返回实时”的逻辑
        backToRealtime();
    } else {
        // 否则，就执行“进入历史模式”的逻辑
        viewHistory();
    }
}


function updateBenchmarkButtonUI(isBenchmarking) {
    const btn = document.getElementById('benchmarkToggleButton');
    if (!btn) return; // 安全检查

    if (isBenchmarking) {
        btn.textContent = '终止测试';
        btn.classList.add('danger'); // 应用 .danger 样式，使其变红
    } else {
        btn.textContent = '启动测试';
        btn.classList.remove('danger'); // 移除 .danger 样式，恢复默认
    }
}
function toggleBenchmarkMode() {
    // 如果 benchmarkPollTimer 存在，说明测试正在运行，需要执行“终止”逻辑
    if (benchmarkPollTimer) {
        clearInterval(benchmarkPollTimer);
        benchmarkPollTimer = null;
        apiPost('/api/command', { cmd: `[ACTION] BENCHMARK_STOP` });
        showNotification('压力测试', '已发送终止指令', 'warning', 3000);
        updateBenchmarkButtonUI(false); // 立即将按钮恢复为“启动”状态
    }
    // 否则，执行“启动”逻辑
    else {
        const n = validateNumber('benchN', 1, 100000, "Benchmark N");
        if (n !== null) {
            showNotification('压力测试', `已启动！正在注入 ${n} 条数据...`, 'success');
            apiPost('/api/command', { cmd: `[ACTION] BENCHMARK N=${n}` })
                .then(() => {
                    benchmarkPollTimer = setInterval(checkBenchmarkStatus, 1500);
                    updateBenchmarkButtonUI(true); // 启动成功后，将按钮变为“终止”状态
                })
                .catch(() => {
                    showNotification('❌ 错误', '压力测试启动失败，请检查连接。', 'danger');
                    // 如果启动失败，确保按钮状态正确
                    updateBenchmarkButtonUI(false);
                });
        }
    }
}
function checkBenchmarkStatus() {
    fetch('/api/command', {
        method: 'POST',
        headers: { 'Content-Type': 'application/json' },
        body: JSON.stringify({ cmd: `[ACTION] BENCHMARK_STATUS` })
    })
        .then(res => res.json())
        .then(data => {
            if (!data.is_running) {
                if (benchmarkPollTimer) {
                    clearInterval(benchmarkPollTimer);
                    benchmarkPollTimer = null;
                    showNotification('压力测试完成', `数据注入成功！`, 'success', 5000);
                    updateBenchmarkButtonUI(false); // 【修改点】测试完成后，重置按钮
                }
            }
        })
        .catch(err => {
            if (benchmarkPollTimer) {
                clearInterval(benchmarkPollTimer);
                benchmarkPollTimer = null;
                showNotification('查询失败', '无法获取压测状态，请检查连接。', 'danger');
                updateBenchmarkButtonUI(false); // 【修改点】查询失败后，也重置按钮
                console.error("Benchmark status check failed:", err);
            }
        });
}


function showHistoryReportInfo() {
    const title = '📂 历史报告说明';
    const htmlContent = `
        <p>当您点击 “导出历史报告” 按钮后，系统将在后台生成一份详细的文本报告。</p>
        <p>报告的默认保存路径为：</p>
        <div class="code-block">HotWordSystem\\build\\report_output.txt</div>
        <div class="info-note">
            <p style="margin:0;"><strong>💡 提示：</strong></p>
            <p style="margin:0;">由于浏览器安全限制，程序无法直接为您打开本地文件。请手动复制上方路径，并在您的文件资源管理器中打开。</p>
        </div>
    `;
    showInfoModal(title, htmlContent);
}
