#!/usr/bin/env python3
"""Detailed pipeline analysis"""
import subprocess
import json
import sys

def analyze(yo_file):
    result = subprocess.run(
        ['./cpu'],
        input=open(yo_file, 'rb').read(),
        capture_output=True
    )
    
    stderr = result.stderr.decode()
    
    # Parse stats
    stats = {}
    for line in stderr.split('\n'):
        if 'Total Cycles:' in line:
            stats['cycles'] = int(line.split(':')[1].strip())
        elif 'Instructions Retired:' in line:
            stats['instructions'] = int(line.split(':')[1].strip())
        elif 'Stall Cycles:' in line:
            stats['stalls'] = int(line.split(':')[1].strip())
        elif 'Bubble Cycles:' in line:
            stats['bubbles'] = int(line.split(':')[1].strip())
    
    n = stats['instructions']
    c = stats['cycles']
    stalls = stats['stalls']
    bubbles = stats['bubbles']
    
    # 5-stage pipeline analysis
    startup = 4  # F,D,E,M stages before first W
    drain = 4    # Last instruction needs 4 more cycles after F
    
    # Theoretical minimum: n + 4 (startup) cycles
    # Or: n instructions * 1 CPI + 4 startup
    theoretical_min = n + 4
    
    # Actual overhead = cycles - theoretical_min
    overhead = c - theoretical_min
    
    # Overhead breakdown
    # - stalls: known
    # - bubbles: known
    # - other: overhead - stalls - bubbles (should be small if our counting is right)
    other = overhead - stalls - bubbles
    
    ipc = n / c if c > 0 else 0
    
    print(f"=== 详细分析: {yo_file} ===")
    print(f"")
    print(f"📊 基本统计")
    print(f"   指令数: {n}")
    print(f"   总周期: {c}")
    print(f"   IPC: {ipc:.4f}")
    print(f"")
    print(f"📈 开销分解")
    print(f"   理论最小周期 (n+4): {theoretical_min}")
    print(f"   实际开销: {overhead} 周期")
    print(f"   ├─ Stall (数据冒险): {stalls} 周期")
    print(f"   ├─ Bubble (控制冒险): {bubbles} 周期")
    print(f"   └─ 其他: {other} 周期")
    print(f"")
    print(f"✅ 结论")
    if stalls >= 0 and bubbles >= 0:
        if other <= 5:
            print(f"   模拟器行为正常！")
            print(f"   - Stall/Bubble 计数器工作正确")
            print(f"   - IPC < 1.0 是正常的流水线开销")
        else:
            print(f"   可能存在未计入的开销")
            print(f"   - 检查是否有额外的 bubble/stall 未被计数")

if __name__ == '__main__':
    yo_file = sys.argv[1] if len(sys.argv) > 1 else 'test/asumr.yo'
    analyze(yo_file)
