#!/usr/bin/env python3
"""
Y86-64 Pipeline Visualizer
可视化流水线执行过程，帮助理解IPC小于1的原因
"""

import json
import sys
import subprocess
import os

def get_instruction_name(icode, ifun=0, rA=15, rB=15):
    """根据指令码返回指令名称"""
    icode_names = {
        0x0: "halt",
        0x1: "nop",
        0x2: "rrmovq" if ifun == 0 else f"cmov{['', 'le', 'l', 'e', 'ne', 'ge', 'g'][ifun]}",
        0x3: "irmovq",
        0x4: "rmmovq",
        0x5: "mrmovq",
        0x6: {0x0: "addq", 0x1: "subq", 0x2: "andq", 0x3: "xorq"}.get(ifun, "opq"),
        0x7: {0x0: "jmp", 0x1: "jle", 0x2: "jl", 0x3: "je", 0x4: "jne", 0x5: "jge", 0x6: "jg"}.get(ifun, "jxx"),
        0x8: "call",
        0x9: "ret",
        0xA: "pushq",
        0xB: "popq",
    }
    
    if icode == 0x2 and ifun != 0:
        return icode_names[icode]
    elif icode == 0x6:
        return icode_names[icode]
    elif icode == 0x7:
        return icode_names[icode]
    else:
        name = icode_names.get(icode, f"icode{icode}")
        if icode in [0x2, 0x4, 0x5, 0x6, 0xA, 0xB]:
            reg_names = ['%rax', '%rcx', '%rdx', '%rbx', '%rsp', '%rbp', '%rsi', '%rdi',
                        '%r8', '%r9', '%r10', '%r11', '%r12', '%r13', '%r14', '']
            if rA != 15:
                name += f" {reg_names[rA]}"
            if rB != 15 and icode != 0x3:
                name += f",{reg_names[rB]}"
            elif icode == 0x3:
                name += f" $imm,%{reg_names[rB]}"
        return name

def parse_instruction_from_pc(mem, pc):
    """从内存中解析指令（简化版）"""
    if pc >= len(mem):
        return "invalid"
    
    byte1 = mem[pc] if pc < len(mem) else 0
    icode = (byte1 >> 4) & 0xF
    ifun = byte1 & 0xF
    
    if icode == 0x0:
        return "halt"
    elif icode == 0x1:
        return "nop"
    elif icode == 0x2:
        return "rrmovq" if ifun == 0 else f"cmov{['', 'le', 'l', 'e', 'ne', 'ge', 'g'][ifun]}"
    elif icode == 0x3:
        return "irmovq"
    elif icode == 0x4:
        return "rmmovq"
    elif icode == 0x5:
        return "mrmovq"
    elif icode == 0x6:
        return {0x0: "addq", 0x1: "subq", 0x2: "andq", 0x3: "xorq"}.get(ifun, "opq")
    elif icode == 0x7:
        return {0x0: "jmp", 0x1: "jle", 0x2: "jl", 0x3: "je", 0x4: "jne", 0x5: "jge", 0x6: "jg"}.get(ifun, "jxx")
    elif icode == 0x8:
        return "call"
    elif icode == 0x9:
        return "ret"
    elif icode == 0xA:
        return "pushq"
    elif icode == 0xB:
        return "popq"
    else:
        return f"icode{icode}"

def visualize_pipeline(test_file, max_cycles=30):
    """可视化流水线执行"""
    # 获取脚本所在目录
    script_dir = os.path.dirname(os.path.abspath(__file__))
    cpu_path = os.path.join(script_dir, 'cpu')
    test_path = os.path.join(script_dir, test_file) if not os.path.isabs(test_file) else test_file
    
    # 运行模拟器
    result = subprocess.run([cpu_path], stdin=open(test_path), 
                          stdout=subprocess.PIPE, stderr=subprocess.PIPE, 
                          cwd=script_dir)
    
    if result.returncode != 0:
        print(f"Error running simulator: {result.stderr.decode()}")
        return
    
    # 解析JSON输出
    try:
        states = json.loads(result.stdout.decode())
    except json.JSONDecodeError as e:
        print(f"Error parsing JSON: {e}")
        print("Output:", result.stdout.decode()[:500])
        return
    
    # 解析性能统计
    perf_stats = {}
    for line in result.stderr.decode().split('\n'):
        if 'Total Cycles:' in line:
            perf_stats['cycles'] = int(line.split(':')[1].strip())
        elif 'Instructions Retired:' in line:
            perf_stats['instructions'] = int(line.split(':')[1].strip())
        elif 'IPC' in line:
            perf_stats['ipc'] = float(line.split(':')[1].strip())
        elif 'Stall Cycles:' in line:
            perf_stats['stall'] = int(line.split(':')[1].strip())
        elif 'Bubble Cycles:' in line:
            perf_stats['bubble'] = int(line.split(':')[1].strip())
    
    print("=" * 80)
    print(f"Pipeline Visualization for: {os.path.basename(test_file)}")
    print("=" * 80)
    print()
    
    # 显示性能统计
    print("Performance Statistics:")
    print(f"  Total Cycles: {perf_stats.get('cycles', 'N/A')}")
    print(f"  Instructions Retired: {perf_stats.get('instructions', 'N/A')}")
    print(f"  IPC: {perf_stats.get('ipc', 'N/A'):.4f}")
    print(f"  Stall Cycles: {perf_stats.get('stall', 'N/A')}")
    print(f"  Bubble Cycles: {perf_stats.get('bubble', 'N/A')}")
    print()
    
    # 分析IPC小于1的原因
    if perf_stats.get('ipc', 1.0) < 1.0:
        print("Why IPC < 1.0:")
        total_cycles = perf_stats.get('cycles', 0)
        instructions = perf_stats.get('instructions', 0)
        stall = perf_stats.get('stall', 0)
        bubble = perf_stats.get('bubble', 0)
        
        if total_cycles > 0:
            startup_overhead = max(0, total_cycles - instructions - stall - bubble)
            print(f"  1. Pipeline Startup: ~{startup_overhead} cycles (filling pipeline)")
            print(f"  2. Stall Cycles: {stall} cycles (data hazards)")
            print(f"  3. Bubble Cycles: {bubble} cycles (control hazards)")
            print(f"  4. Pipeline Drain: ~{max(0, total_cycles - instructions - startup_overhead - stall - bubble)} cycles (draining pipeline)")
        print()
    
    # 显示PC序列
    print("PC Sequence (showing instruction completion order):")
    print("-" * 80)
    pc_sequence = [hex(s['PC']) for s in states]
    for i, pc in enumerate(pc_sequence[:20]):  # 只显示前20个
        print(f"  State {i}: PC = {pc}")
    if len(pc_sequence) > 20:
        print(f"  ... ({len(pc_sequence) - 20} more states)")
    print()
    
    # 分析流水线执行模式
    print("Pipeline Execution Analysis:")
    print("-" * 80)
    
    # 计算理论最小周期数
    num_instructions = len(states)
    theoretical_min_cycles = num_instructions + 4  # 5级流水线：4个启动周期 + N个指令周期
    
    print(f"Theoretical minimum cycles (ideal pipeline): {theoretical_min_cycles}")
    print(f"Actual cycles: {perf_stats.get('cycles', 0)}")
    print(f"Overhead: {perf_stats.get('cycles', 0) - theoretical_min_cycles} cycles")
    print()
    
    # 分析IPC损失的原因
    print("IPC Breakdown:")
    total_cycles = perf_stats.get('cycles', 0)
    instructions = perf_stats.get('instructions', 0)
    stall = perf_stats.get('stall', 0)
    bubble = perf_stats.get('bubble', 0)
    
    if total_cycles > 0:
        # 理想情况：每个周期完成一条指令
        ideal_cycles = instructions
        
        # 实际损失
        startup_loss = max(0, 4)  # 前4个周期填满流水线
        stall_loss = stall
        bubble_loss = bubble
        drain_loss = max(0, total_cycles - instructions - startup_loss - stall_loss - bubble_loss)
        
        print(f"  Ideal cycles (if IPC=1.0): {ideal_cycles}")
        print(f"  Pipeline startup overhead: {startup_loss} cycles ({startup_loss/total_cycles*100:.1f}%)")
        print(f"  Stall cycles (data hazards): {stall_loss} cycles ({stall_loss/total_cycles*100:.1f}%)")
        print(f"  Bubble cycles (control hazards): {bubble_loss} cycles ({bubble_loss/total_cycles*100:.1f}%)")
        print(f"  Pipeline drain overhead: {drain_loss} cycles ({drain_loss/total_cycles*100:.1f}%)")
        print(f"  Total overhead: {startup_loss + stall_loss + bubble_loss + drain_loss} cycles")
    print()
    
    # 简化的流水线时间线
    print("Pipeline Timeline (showing when each instruction completes):")
    print("-" * 80)
    print("Cycle | Instruction Completion | Cumulative IPC")
    print("-" * 80)
    
    # 基于PC序列推断指令完成时间
    # 简化假设：每条指令在记录状态时完成
    for i, state in enumerate(states[:max_cycles]):
        pc = hex(state['PC'])
        # 估算完成周期（简化：假设指令在状态记录时完成）
        # 实际完成周期需要考虑流水线深度
        estimated_cycle = min(i + 5, total_cycles)  # 5级流水线，第i条指令大约在i+5周期完成
        current_ipc = (i + 1) / estimated_cycle if estimated_cycle > 0 else 0
        print(f"  {estimated_cycle:3d} | I{i} completes (PC={pc}) | IPC={current_ipc:.3f}")
    
    print("-" * 80)
    print()
    print("Note: This is a simplified visualization.")
    print("For detailed pipeline state, we would need to track F/D/E/M/W registers.")

if __name__ == '__main__':
    if len(sys.argv) < 2:
        print("Usage: python3 visualize.py <test_file.yo> [max_cycles]")
        sys.exit(1)
    
    test_file = sys.argv[1]
    max_cycles = int(sys.argv[2]) if len(sys.argv) > 2 else 30
    
    visualize_pipeline(test_file, max_cycles)

