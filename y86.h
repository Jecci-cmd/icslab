#ifndef Y86_H
#define Y86_H

#include <cstdint>
#include <string>
#include <map>
#include <vector>

// Y86-64 指令码定义
namespace Y86 {
    // 指令码 (icode)
    constexpr uint8_t HALT = 0x0;
    constexpr uint8_t NOP = 0x1;
    constexpr uint8_t RRMOVQ = 0x2;
    constexpr uint8_t IRMOVQ = 0x3;
    constexpr uint8_t RMMOVQ = 0x4;
    constexpr uint8_t MRMOVQ = 0x5;
    constexpr uint8_t OPQ = 0x6;
    constexpr uint8_t JXX = 0x7;
    constexpr uint8_t CALL = 0x8;
    constexpr uint8_t RET = 0x9;
    constexpr uint8_t PUSHQ = 0xA;
    constexpr uint8_t POPQ = 0xB;
    constexpr uint8_t CMOVXX = 0x2;  // 与RRMOVQ相同，通过ifun区分

    // 功能码 (ifun) - 用于OPQ和JXX
    constexpr uint8_t ADD = 0x0;
    constexpr uint8_t SUB = 0x1;
    constexpr uint8_t AND = 0x2;
    constexpr uint8_t XOR = 0x3;
    
    // 条件码
    constexpr uint8_t C_YES = 0x0;  // 无条件
    constexpr uint8_t C_LE = 0x1;
    constexpr uint8_t C_L = 0x2;
    constexpr uint8_t C_E = 0x3;
    constexpr uint8_t C_NE = 0x4;
    constexpr uint8_t C_GE = 0x5;
    constexpr uint8_t C_G = 0x6;

    // 寄存器编码
    constexpr uint8_t RAX = 0x0;
    constexpr uint8_t RCX = 0x1;
    constexpr uint8_t RDX = 0x2;
    constexpr uint8_t RBX = 0x3;
    constexpr uint8_t RSP = 0x4;
    constexpr uint8_t RBP = 0x5;
    constexpr uint8_t RSI = 0x6;
    constexpr uint8_t RDI = 0x7;
    constexpr uint8_t R8 = 0x8;
    constexpr uint8_t R9 = 0x9;
    constexpr uint8_t R10 = 0xA;
    constexpr uint8_t R11 = 0xB;
    constexpr uint8_t R12 = 0xC;
    constexpr uint8_t R13 = 0xD;
    constexpr uint8_t R14 = 0xE;
    constexpr uint8_t RNONE = 0xF;

    // 状态码
    constexpr uint8_t STAT_AOK = 1;  // 正常
    constexpr uint8_t STAT_HLT = 2;  // 停机
    constexpr uint8_t STAT_ADR = 3;  // 地址错误
    constexpr uint8_t STAT_INS = 4;  // 非法指令

    // 寄存器名称映射
    extern const std::map<uint8_t, std::string> REG_NAMES;
    
    // 获取寄存器名称
    std::string getRegName(uint8_t reg);
}

// 条件码结构
struct ConditionCodes {
    bool ZF = false;  // Zero Flag
    bool SF = false;  // Sign Flag
    bool OF = false;  // Overflow Flag
};

// 寄存器文件（15个通用寄存器 + RNONE）
class RegisterFile {
public:
    int64_t regs[15];  // RAX到R14
    
    RegisterFile();
    int64_t get(uint8_t reg) const;
    void set(uint8_t reg, int64_t val);
    void reset();
};

// 内存（模拟大端序，但实际按小端序处理）
class Memory {
public:
    static constexpr size_t MEM_SIZE = 1024 * 1024;  // 1MB
    uint8_t mem[MEM_SIZE];
    
    Memory();
    uint64_t read64(uint64_t addr) const;
    void write64(uint64_t addr, uint64_t val);
    void reset();
    // 获取所有非零内存值（用于输出）
    std::map<uint64_t, int64_t> getNonZeroMemory() const;
};

#endif // Y86_H

