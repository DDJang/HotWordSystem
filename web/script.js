var feedbackTimer = null;
var isHistoryMode = false;
var historyWindowText = "";

var myChart = echarts.init(document.getElementById('mainChart'));
myChart.setOption({
    tooltip: { trigger: 'axis' },
    grid: { left: '3%', right: '5%', bottom: '13%', containLabel: true, top: '10%' },
    xAxis: { type: 'category', data: [] },
    yAxis: { type: 'value' },
    universalTransition: { enabled: true, duration: 500 },
    series: [{ type: 'bar', data: [], itemStyle: { color: '#3b82f6', borderRadius: [4, 4, 0, 0] }, label: { show: true, position: 'top' } }]
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

    // 【修改点】不再创建 button，直接填入内容
    // 点击卡片本身也可以关闭（可选，增加交互性）
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
    fetch('/api/data?k=' + k)
        .then(res => res.json())
        .then(data => {
            if (data.shutdown) {
                clearInterval(mainInterval);
                document.body.innerHTML = "<div style='display:flex;justify-content:center;align-items:center;height:100vh;flex-direction:column;color:#555;'><h1>🚫 系统已关闭</h1><p>连接已断开，请手动重启服务。</p></div>";
                return;
            }


            // 【新增】检查驱逐标志并显示通知
            // 危险警告优先
            if (data.capacity_limit_evicted) {
                showNotification(
                    '内存警告',
                    '系统内存占用已达上限！为保证稳定性，部分最老的数据已被强制清除，这些数据将无法回溯。',
                    'danger',
                    10000 // 危险警告显示更久
                );
            } else if (data.time_limit_evicted) {
                showNotification(
                    '数据清理提示',
                    '部分历史数据因超出设置的最大保留时间(“Storage”)，已被常规清除，这部分数据将无法回溯。',
                    'warning'
                );
            }

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

            currentBackendTimestamp = data.current_ts;
            if (!isHistoryMode) {
                document.getElementById('windowInfo').innerText =
                    `Window: [${data.window_start} ~ ${data.window_end}] | Now: ${data.current_ts}`;
            } else {
                document.getElementById('windowInfo').innerText =
                    `${historyWindowText} | Now: ${data.current_ts}`;
            }

            document.getElementById('retentionInfo').innerText = `Retention: ${data.retention_sec} s`;

            if (isInitialLoad) {
                log("✅ System Ready & Connected", currentBackendTimestamp);
                isInitialLoad = false;
            }
        });
}

var mainInterval = setInterval(() => {
    fetchAndUpdateRealtimeChart();
    fetch('/api/trends?k=10').then(r => r.json()).then(list => renderTrends(list));
}, 1000);


function validateNumber(id, min, max, name) {
    const el = document.getElementById(id);
    const val = parseInt(el.value);
    if (isNaN(val)) {
        alert(`❌ [${name}] 请输入有效的数字`);
        return null;
    }
    if (val < min || val > max) {
        alert(`❌ [${name}] 数值必须在 ${min} ~ ${max} 之间`);
        return null;
    }
    return val;
}

function validateTime(id) {
    const el = document.getElementById(id);
    const val = el.value.trim();
    const regex = /^\d{1,2}:\d{1,2}:\d{1,2}$/;
    if (!regex.test(val)) {
        alert(`❌ 时间格式错误: ${val}\n请使用 HH:MM:SS (例如 12:30:00)`);
        return null;
    }
    return val;
}

function togglePosAll(el, suppressUpdate = false) {
    const isAll = el.checked;
    const configArea = document.getElementById('posConfigArea');
    if (isAll) {
        configArea.style.opacity = "0.4";
        configArea.style.pointerEvents = "none";
    } else {
        configArea.style.opacity = "1";
        configArea.style.pointerEvents = "auto";
    }
    if (!suppressUpdate) {
        updateConfig();
    }
}

function toggleSensitiveAll(el, suppressUpdate = false) {
    const isAll = el.checked;
    const configArea = document.getElementById('sensitiveConfigArea');
    if (isAll) {
        configArea.style.opacity = "0.4";
        configArea.style.pointerEvents = "none";
    } else {
        configArea.style.opacity = "1";
        configArea.style.pointerEvents = "auto";
    }
    if (!suppressUpdate) {
        updateConfig();
    }
}

function updateConfig() {
    const allowPosAll = document.getElementById('cb_all').checked;
    const allowSensitiveAll = document.getElementById('cb_sensitive_allow').checked;
    const checks = document.querySelectorAll('.pos-item:checked');
    let tags = [];
    checks.forEach(c => tags.push(c.value));

    apiPost('/api/config', {
        allow_all: allowPosAll,
        allow_sensitive: allowSensitiveAll,
        tags: tags
    });

    showNotification('配置已更新', '新的过滤规则已应用，从当前时间戳开始生效。', 'success', 3000);
}

function loadConfigState() {
    fetch('/api/config').then(r => r.json()).then(d => {
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

        const cbPos = document.getElementById('cb_all');
        cbPos.checked = d.allow_all;
        togglePosAll(cbPos, true);

        const cbSens = document.getElementById('cb_sensitive_allow');
        cbSens.checked = !!d.allow_sensitive;
        toggleSensitiveAll(cbSens, true);

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
    const threshold = parseFloat(document.getElementById('trendThreshold').value) || 0;
    tbody.innerHTML = "";
    let rank = 1;
    list.forEach(item => {
        if (Math.abs(item.score) < threshold) return;
        const tr = document.createElement('tr');
        const trendClass = item.score > 0 ? "trend-up" : "trend-down";
        tr.innerHTML = `<td>${rank++}</td><td style="font-weight:bold;">${item.word}</td><td class="${trendClass}">${item.score.toFixed(2)}</td><td class="${trendClass}">${item.score > 0 ? "🔥" : "❄️"}</td>`;
        tbody.appendChild(tr);
    });
}

function apiPost(url, data) {
    log("Sending...");
    fetch(url, { method: 'POST', headers: { 'Content-Type': 'application/json' }, body: JSON.stringify(data) })
        .then(r => r.json())
        .then(d => {
            const serverTime = d.timestamp || null;
            log("✅ " + (d.message || "OK"), serverTime);
        })
        .catch(e => log("❌ " + e));
}
function log(msg, customTime = null) {
    const el = document.getElementById('log');
    const timeStr = customTime ? customTime : new Date().toLocaleTimeString();
    el.innerText = `[${timeStr}] ${msg}`;
}

function updateWindowSize() {
    const s = validateNumber('winSize', 1, 2592000, "Window Size");
    if (s !== null) {
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

    // 3. 【核心修复】预检查：Window 的值是否已经超过了系统允许的最大值？
    if (winSize > retMaxHardcoded) {
        alert(`❌ 操作无效：窗口大小过大\n\n您在 "Window" 中输入的值 (${winSize}s) 已超过系统最大限制 (${retMaxHardcoded}s)。\n\n请先设置一个有效的窗口大小。`);
        return; // 终止函数，不继续执行
    }

    // 4. 计算用于验证的有效最小值 (effectiveMin)。
    //    它应该是 "Window" 的值和 "Storage" 硬编码最小值中，较大的那一个。
    const effectiveMin = Math.max(winSize, retMinHardcoded);

    // 5. 使用计算出的有效最小值进行验证
    const r = validateNumber('retSize', effectiveMin, retMaxHardcoded, "Retention");

    if (r !== null) {
        apiPost('/api/command', { cmd: `[ACTION] SET_RETENTION R=${r}` });
        showNotification('存储设置', `数据保留时间已更新为 ${r} 秒`, 'success');
    }
}

function sendManualData() {
    const val = document.getElementById('manualInput').value;
    if (val) {
        apiPost('/api/command', { cmd: val });
        document.getElementById('manualInput').value = "";
        showNotification('数据发送', '模拟数据/指令已发送至服务器', 'success');

        // 【新增】检查是否为 SHUTDOWN 指令
        const shutdownRegex = /\[ACTION\]\s+SHUTDOWN/i;
        if (shutdownRegex.test(val)) {
            setTimeout(() => {
                document.body.innerHTML = "<div style='display:flex;justify-content:center;align-items:center;height:100vh;flex-direction:column;color:#555;'><h1>🚫 系统已关闭</h1><p>连接已断开，请手动重启服务。</p></div>";
            }, 1000);
        }
    }
}

function addSensitive() {
    const w = document.getElementById('newSensitive').value;
    if (w) {
        apiPost('/api/config', { add_sensitive: w });
        document.getElementById('newSensitive').value = "";
        setTimeout(loadConfigState, 500);
        showNotification('屏蔽词添加成功', `已将 "${w}" 加入屏蔽列表。`, 'success');
    }
}
function removeSensitive(w) {
    if (confirm("Remove " + w + "?")) {
        apiPost('/api/config', { remove_sensitive: w });
        setTimeout(loadConfigState, 500);
        showNotification('屏蔽词已移除', `"${w}" 已从屏蔽列表中删除。`, 'success');
    }
}

function viewHistory() {
    const s = validateTime('histStart');
    const e = validateTime('histEnd');
    if (!s || !e) return;

    const k = document.getElementById('displayK').value || 10;

    isHistoryMode = true;
    historyWindowText = `MODE: HISTORY [${s} ~ ${e}]`;
    document.getElementById('windowInfo').innerText =
        `${historyWindowText} | Now: ${currentBackendTimestamp}`;

    log(`Loading History...`, currentBackendTimestamp);

    document.getElementById('btnBackRealtime').classList.add('btn-pulse');
    document.getElementById('history-mode-overlay').classList.add('show');

    showNotification('历史回放', `已进入回放模式 [${s} ~ ${e}]，实时更新暂停`, 'warning', 4000);

    fetch(`/api/history_view?start=${encodeURIComponent(s)}&end=${encodeURIComponent(e)}&k=${k}`)
        .then(r => r.json())
        .then(d => {
            const historySeriesData = d.values.map((value, index) => ({
                value: value,
                id: d.categories[index]
            }));

            myChart.setOption({
                xAxis: { data: d.categories },
                series: [{
                    data: historySeriesData,
                    itemStyle: { color: '#8b5cf6' }
                }],
                grid: {
                    top: 80
                }
            });
        });
}

function backToRealtime() {
    isHistoryMode = false;
    log("Back to Realtime.", currentBackendTimestamp);
    document.getElementById('btnBackRealtime').classList.remove('btn-pulse');
    document.getElementById('history-mode-overlay').classList.remove('show');
    myChart.setOption({
        series: [{ itemStyle: { color: '#3b82f6' } }],
        grid: { top: '10%' }
    });

    showNotification('实时监控', '已退出回放模式，恢复实时数据流', 'success');

    fetchAndUpdateRealtimeChart();
}

function showStats() {
    fetch('/api/stats').then(r => r.json()).then(d => {
        document.getElementById('st_time').innerText = d.runtime.toFixed(2) + " s";
        document.getElementById('st_lines').innerText = d.lines;
        document.getElementById('st_words').innerText = d.words;
        document.getElementById('st_qps').innerText = d.qps.toFixed(2);
        document.getElementById('st_mem').innerText = d.memory.toFixed(2) + " MB";
        document.getElementById('statsModal').style.display = 'flex';
    });
}
function closeStats(e) {
    if (!e || e.target.id === 'statsModal') document.getElementById('statsModal').style.display = 'none';
}

function genReport() {
    const s = validateTime('histStart');
    const e = validateTime('histEnd');
    if (s && e) {
        apiPost('/api/command', { cmd: `[ACTION] HISTORY START=${s} END=${e} STEP=60` });
        showNotification('导出报告', '正在后台生成历史数据报告，请查看服务器日志', 'success');
    }
}

function runBench() {
    const n = validateNumber('benchN', 1, 100000, "Benchmark N");
    if (n !== null) {
        apiPost('/api/command', { cmd: `[ACTION] BENCHMARK N=${n}` });
        showNotification('压力测试', `已启动压测，目标写入 ${n} 条数据`, 'warning');
    }
}

function resetSystem() {
    if (!confirm("⚠️ 警告：这将清空所有内存数据和历史日志文件，并会中断压力测试！")) return;
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
            document.getElementById('btnBackRealtime').classList.remove('btn-pulse');
            document.getElementById('history-mode-overlay').classList.remove('show');
            log("✅ System & Log Cleared.", "00:00:00");
            showNotification('系统重置', '内存数据及日志文件已全部清空', 'danger', 6000);
        })
        .catch(e => {
            log("❌ " + e);
            showNotification('重置失败', '连接服务器时发生错误', 'danger');
        });
}

function shutdownSystem() {
    if (!confirm("⚠️ 严重警告 ⚠️\n\n这将完全关闭服务器程序！\n所有连接将断开，您必须手动去服务器重启程序。\n\n确定要关闭吗？")) {
        return;
    }
    apiPost('/api/command', { cmd: `[ACTION] SHUTDOWN` });
    showNotification('系统关闭', '正在断开连接并停止服务...', 'danger', 10000);
    setTimeout(() => {
        document.body.innerHTML = "<div style='display:flex;justify-content:center;align-items:center;height:100vh;flex-direction:column;color:#555;'><h1>🚫 系统已关闭</h1><p>连接已断开，请手动重启服务。</p></div>";
    }, 1000);
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