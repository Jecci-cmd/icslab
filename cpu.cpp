#include "pipeline.h"
#include <iostream>
#include <sstream>
#include <string>
#include <vector>
#include <map>
#include <iomanip>
#include <algorithm>
#include <cstdio>
#include <iomanip>

// JSON输出辅助函数
void outputJSON(const PipelineSimulator::State& state) {
    std::cout << "    {\n";
    
    // PC
    std::cout << "        \"PC\": " << state.PC << ",\n";
    
    // REG
    std::cout << "        \"REG\": {\n";
    bool first_reg = true;
    for (int i = 0; i < 15; i++) {
        if (!first_reg) std::cout << ",\n";
        std::string regName = Y86::getRegName(i);
        std::cout << "            \"" << regName << "\": " << state.regs.get(i);
        first_reg = false;
    }
    std::cout << "\n        },\n";
    
    // MEM
    std::cout << "        \"MEM\": {\n";
    bool first_mem = true;
    for (const auto& pair : state.mem_snapshot) {
        if (!first_mem) std::cout << ",\n";
        std::cout << "            \"" << pair.first << "\": " << pair.second;
        first_mem = false;
    }
    std::cout << "\n        },\n";
    
    // CC
    std::cout << "        \"CC\": {\n";
    std::cout << "            \"ZF\": " << (state.CC.ZF ? 1 : 0) << ",\n";
    std::cout << "            \"SF\": " << (state.CC.SF ? 1 : 0) << ",\n";
    std::cout << "            \"OF\": " << (state.CC.OF ? 1 : 0) << "\n";
    std::cout << "        },\n";
    
    // STAT
    std::cout << "        \"STAT\": " << static_cast<int>(state.STAT) << "\n";
    
    std::cout << "    }";
}

// 解析.yo文件格式
std::vector<uint8_t> parseYoFile(std::istream& input) {
    // 使用map来存储地址到字节的映射，然后转换为vector
    std::map<uint64_t, uint8_t> addr_map;
    std::string line;
    
    while (std::getline(input, line)) {
        // 跳过注释和空行
        if (line.empty() || line[0] == '#' || line.find('|') == std::string::npos) {
            continue;
        }
        
        // 找到冒号
        size_t colon_pos = line.find(':');
        if (colon_pos == std::string::npos) continue;
        
        // 提取地址（冒号前的部分）
        std::string addr_str = line.substr(0, colon_pos);
        // 移除0x前缀和空格
        size_t hex_start = addr_str.find("0x");
        if (hex_start == std::string::npos) continue;
        addr_str = addr_str.substr(hex_start + 2);
        // 移除空格
        addr_str.erase(std::remove_if(addr_str.begin(), addr_str.end(), 
                     [](char c) { return c == ' ' || c == '\t'; }), addr_str.end());
        
        uint64_t addr = 0;
        try {
            addr = std::stoull(addr_str, nullptr, 16);
        } catch (...) {
            continue;
        }
        
        // 提取十六进制字节（冒号后到|之前的部分）
        std::string hex_part = line.substr(colon_pos + 1);
        // 移除注释部分（|之后的内容）
        size_t pipe_pos = hex_part.find('|');
        if (pipe_pos != std::string::npos) {
            hex_part = hex_part.substr(0, pipe_pos);
        }
        
        // 移除所有空格
        hex_part.erase(std::remove_if(hex_part.begin(), hex_part.end(), 
                     [](char c) { return c == ' ' || c == '\t'; }), hex_part.end());
        
        // 按两个字符一组解析十六进制字节，加载到指定地址
        for (size_t i = 0; i + 1 < hex_part.length(); i += 2) {
            std::string hex_byte = hex_part.substr(i, 2);
            try {
                uint8_t byte = static_cast<uint8_t>(std::stoul(hex_byte, nullptr, 16));
                addr_map[addr + (i / 2)] = byte;
            } catch (...) {
                // 忽略无效的十六进制
                break;
            }
        }
    }
    
    // 将地址映射转换为vector（找到最大地址）
    uint64_t max_addr = 0;
    for (const auto& pair : addr_map) {
        if (pair.first > max_addr) {
            max_addr = pair.first;
        }
    }
    
    // 创建vector，初始化为0
    std::vector<uint8_t> program(max_addr + 1, 0);
    
    // 填充字节
    for (const auto& pair : addr_map) {
        if (pair.first < program.size()) {
            program[pair.first] = pair.second;
        }
    }
    
    return program;
}

int main() {
    // 从stdin读取.yo格式文件
    std::vector<uint8_t> program = parseYoFile(std::cin);
    
    if (program.empty()) {
        std::cerr << "Error: No program loaded" << std::endl;
        return 1;
    }
    
    // 创建模拟器并加载程序
    PipelineSimulator simulator;
    simulator.loadProgram(program);
    
    // 运行模拟器
    simulator.run();
    
    // 输出JSON结果
    const auto& states = simulator.getStates();
    std::cout << "[\n";
    for (size_t i = 0; i < states.size(); i++) {
        if (i > 0) std::cout << ",\n";
        // 直接使用states[i]的引用，避免复制
        outputJSON(states[i]);
    }
    std::cout << "\n]" << std::endl;
    
    // 输出性能统计（到stderr，不影响JSON输出）
    auto stats = simulator.getPerformanceStats();
    std::cerr << "\n=== Performance Statistics ===" << std::endl;
    std::cerr << "Total Cycles: " << stats.total_cycles << std::endl;
    std::cerr << "Instructions Retired: " << stats.instructions_retired << std::endl;
    std::cerr << "IPC (Instructions Per Cycle): " << std::fixed << std::setprecision(4) 
              << stats.ipc << std::endl;
    std::cerr << "Stall Cycles: " << stats.stall_cycles << std::endl;
    std::cerr << "Bubble Cycles: " << stats.bubble_cycles << std::endl;
    
    return 0;
}
