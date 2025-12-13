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

function showConfigFeedback() {
    const feedbackEl = document.getElementById('configFeedback');
    if (feedbackTimer) {
        clearTimeout(feedbackTimer);
    }
    feedbackEl.classList.add('show');
    feedbackTimer = setTimeout(() => {
        feedbackEl.classList.remove('show');
    }, 2500);
}


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
    showConfigFeedback();
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
    const s = validateNumber('winSize', 1, 86400, "Window Size");
    if (s !== null) apiPost('/api/command', { cmd: `[ACTION] SET_WINDOW S=${s}` });
}

function updateRetention() {
    const winSize = parseInt(document.getElementById('winSize').value) || 0;
    const r = validateNumber('retSize', winSize, 2592000, "Retention");
    if (r !== null) apiPost('/api/command', { cmd: `[ACTION] SET_RETENTION R=${r}` });
}
function sendManualData() {
    const val = document.getElementById('manualInput').value;
    if (val) {
        apiPost('/api/command', { cmd: val }); document.getElementById('manualInput').value = "";
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
        showConfigFeedback();
    }
}
function removeSensitive(w) {
    if (confirm("Remove " + w + "?")) {
        apiPost('/api/config', { remove_sensitive: w });
        setTimeout(loadConfigState, 500);
        showConfigFeedback();
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
    }
}

function runBench() {
    const n = validateNumber('benchN', 1, 100000, "Benchmark N");
    if (n !== null) apiPost('/api/command', { cmd: `[ACTION] BENCHMARK N=${n}` });
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
            myChart.setOption({ xAxis: { data: [] }, series: [{ data: [], itemStyle: { color: '#3b82f6' } }] });
            document.getElementById('trendBody').innerHTML = "";
            document.getElementById('windowInfo').innerText = "Window: [Reset] | Now: 00:00:00";
            // 3. 【核心修复】在这里，手动移除“返回实时”按钮的脉冲效果
            document.getElementById('btnBackRealtime').classList.remove('btn-pulse');

            // 4. (可选但推荐) 同时，也应该隐藏历史模式的提示条
            document.getElementById('history-mode-overlay').classList.remove('show');
            log("✅ System & Log Cleared.", "00:00:00");
        })
        .catch(e => {
            log("❌ " + e);
        });
}

function shutdownSystem() {
    if (!confirm("⚠️ 严重警告 ⚠️\n\n这将完全关闭服务器程序！\n所有连接将断开，您必须手动去服务器重启程序。\n\n确定要关闭吗？")) {
        return;
    }
    apiPost('/api/command', { cmd: `[ACTION] SHUTDOWN` });
    setTimeout(() => {
        document.body.innerHTML = "<div style='display:flex;justify-content:center;align-items:center;height:100vh;flex-direction:column;color:#555;'><h1>🚫 系统已关闭</h1><p>连接已断开，请手动重启服务。</p></div>";
    }, 1000);
}