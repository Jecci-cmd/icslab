# Y86-64 Pipeline Simulator

一个完整的Y86-64指令集流水线模拟器实现，支持5级流水线、数据转发、冒险检测等核心功能。

## 📁 项目文件结构

### 核心源代码文件
- **`pipeline.h` / `pipeline.cpp`** - 流水线模拟器核心实现（808行）
  - 5个流水线阶段：Fetch、Decode、Execute、Memory、WriteBack
  - 数据转发（Forwarding）
  - 冒险检测（Load/Use Hazard、Control Hazard）
  - 流水线气泡（Bubble）和停顿（Stall）

- **`y86.h` / `y86.cpp`** - Y86-64指令集定义和内存/寄存器实现

- **`cpu.cpp` / `cpu.h`** - 主程序入口，JSON输出格式化

- **`Makefile`** - 编译配置

### 测试文件
- **`test/`** - 21个测试用例（`.yo`文件）
  - `prog1-prog10` - 基础功能测试
  - `j-cc` - 条件跳转测试
  - `ret-hazard` - RET指令冒险测试
  - `asum*` - 数组求和（递归/迭代/条件移动）
  - `abs-asum-*` - 绝对值求和

- **`answer/`** - 21个标准答案（`.json`文件）

- **`test.py`** - 自动化测试脚本

### 其他文件
- `README.md` - 项目说明
- `test.sh` - 快速测试脚本
- `.gitignore` - Git忽略配置

## 🧪 命令行测试方式

### 1. 编译项目
```bash
make clean && make
```

### 2. 运行单个测试
```bash
# 运行单个测试文件，输出JSON
./cpu < test/prog1.yo > output.json

# 查看JSON输出
cat output.json | python3 -m json.tool

# 对比PC序列
./cpu < test/j-cc.yo 2>/dev/null | python3 -c "
import json, sys
data = json.load(sys.stdin)
print('PC sequence:', [hex(s['PC']) for s in data])
"
```

### 3. 运行完整测试套件
```bash
# 使用官方测试脚本（推荐）
python3 test.py --bin ./cpu

# 或者手动测试所有用例
for f in test/*.yo; do
    name=$(basename "$f" .yo)
    ./cpu < "$f" 2>/dev/null > /tmp/${name}.json
    python3 -c "
import json
a = json.load(open('/tmp/${name}.json'))
b = json.load(open('answer/${name}.json'))
print('${name}:', 'PASS' if a==b else 'FAIL')
" 2>/dev/null
done
```

### 4. 性能统计
```bash
# 查看性能统计（输出到stderr）
./cpu < test/asumr.yo 2>&1 | grep -A5 "Performance"
```

## 🚀 相比单周期模拟器的优势

### 1. 性能提升
- **单周期**：每条指令需要5个周期（F+D+E+M+W）
- **流水线**：理想情况下IPC接近1.0（每条指令1个周期）
- **实际测试**：IPC约0.5-0.7（受冒险影响）

### 2. 真实硬件模拟
- **数据冒险**：Load/Use Hazard检测和Stall
- **控制冒险**：JXX预测失败、RET指令处理
- **数据转发**：E/M和M/W阶段转发，减少Stall
- **流水线气泡**：正确处理控制流变化

### 3. 复杂度对比
- **单周期**：约200-300行代码
- **流水线**：约800行代码 + 复杂的状态管理

## 📊 PPT内容建议

### 1. 项目概述（2-3页）
- Y86-64指令集简介
- 流水线模拟器目标
- 项目结构

### 2. 核心实现（5-6页）
- **5级流水线架构图**
  ```
  Fetch → Decode → Execute → Memory → WriteBack
    ↓       ↓        ↓         ↓          ↓
   F/D    D/E      E/M       M/W
  ```
- **数据转发机制（Forwarding）**
  - E/M → D/E转发
  - M/W → D/E转发
- **冒险检测与处理**
  - Load/Use Hazard → Stall
  - Control Hazard → Bubble/Flush
- 关键代码片段展示

### 3. 测试与验证（2-3页）
- 21个测试用例覆盖
- 测试结果（全部通过）
- 性能统计（IPC、Stall周期等）

### 4. 优势分析（2页）
- 相比单周期模拟器的性能提升
- 真实硬件行为模拟
- 复杂度对比

### 5. 难点与解决方案（2-3页）
- 问题1：SUB指令溢出检测错误
- 问题2：CMOVXX条件转发
- 问题3：RET指令PC更新时机
- 问题4：HALT后停止取指

### 6. 总结与展望（1页）
- 项目总结
- 可能的改进方向

## 📝 快速开始

```bash
# 编译
make

# 运行测试
python3 test.py --bin ./cpu

# 预期输出：All correct!
```

## 📄 许可证

本项目为课程作业实现，仅供学习参考。
