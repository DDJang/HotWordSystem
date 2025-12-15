import requests
import time
import random
import json
import sys
import os

# 配置
SERVER_URL = "http://localhost:8080/api/command"
DATA_FILE = os.path.join(os.path.dirname(__file__), "test_data.txt")
MIN_DELAY = 0.5  # 最小间隔(秒)
MAX_DELAY = 2.0  # 最大间隔(秒) - 模拟自然输入的不规律性

def run_simulation():
    print(f"🚀 开始模拟数据流...")
    print(f"📡 目标服务器: {SERVER_URL}")
    print(f"📂 数据文件: {DATA_FILE}")
    print("-" * 30)

    try:
        with open(DATA_FILE, 'r', encoding='utf-8') as f:
            lines = f.readlines()
    except FileNotFoundError:
        print(f"❌ 错误: 找不到文件 {DATA_FILE}")
        return

    count = 0
    for line in lines:
        line = line.strip()
        if not line:
            continue

        # 构造 JSON payload
        # C++ 后端接收 {"cmd": "..."}
        payload = {"cmd": line}

        try:
            # 发送 POST 请求
            response = requests.post(SERVER_URL, json=payload)
            
            if response.status_code == 200:
                count += 1
                print(f"[{count}] ✅ 发送成功: {line}")
            else:
                print(f"[{count}] ⚠️ 发送失败 (Status {response.status_code}): {line}")

        except requests.exceptions.ConnectionError:
            print(f"❌ 连接错误: 无法连接到 C++ 服务器。请确认程序已在运行且端口为 8080。")
            break
        except Exception as e:
            print(f"❌ 发生异常: {e}")

        # 模拟随机延迟，让数据流看起来更像真实的“流”
        delay = random.uniform(MIN_DELAY, MAX_DELAY)
        time.sleep(delay)

    print("-" * 30)
    print("🏁 模拟结束，所有数据已发送。")

if __name__ == "__main__":
    # 检查是否安装了 requests 库
    try:
        import requests
    except ImportError:
        print("❌ 缺少 requests 库。请运行: pip install requests")
        sys.exit(1)

    run_simulation()