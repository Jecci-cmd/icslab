#include "y86.h"
#include <stdexcept>

namespace Y86 {
    const std::map<uint8_t, std::string> REG_NAMES = {
        {RAX, "rax"}, {RCX, "rcx"}, {RDX, "rdx"}, {RBX, "rbx"},
        {RSP, "rsp"}, {RBP, "rbp"}, {RSI, "rsi"}, {RDI, "rdi"},
        {R8, "r8"}, {R9, "r9"}, {R10, "r10"}, {R11, "r11"},
        {R12, "r12"}, {R13, "r13"}, {R14, "r14"}
    };

    std::string getRegName(uint8_t reg) {
        if (reg == RNONE) return "";
        auto it = REG_NAMES.find(reg);
        if (it != REG_NAMES.end()) return it->second;
        return "";
    }
}

// RegisterFile 实现
RegisterFile::RegisterFile() {
    reset();
}

int64_t RegisterFile::get(uint8_t reg) const {
    if (reg == Y86::RNONE) return 0;
    if (reg >= 15) return 0;
    return regs[reg];
}

void RegisterFile::set(uint8_t reg, int64_t val) {
    if (reg == Y86::RNONE) return;
    if (reg >= 15) return;
    regs[reg] = val;
}

void RegisterFile::reset() {
    for (int i = 0; i < 15; i++) {
        regs[i] = 0;
    }
}

// Memory 实现
Memory::Memory() {
    reset();
}

uint64_t Memory::read64(uint64_t addr) const {
    // 边界检查：addr 必须 < MEM_SIZE - 7 才能读取 8 字节
    // 使用此检查避免 addr + 8 溢出的问题
    if (addr >= MEM_SIZE || addr > MEM_SIZE - 8) {
        throw std::runtime_error("Memory read out of bounds");
    }
    uint64_t val = 0;
    // 小端序读取
    for (int i = 0; i < 8; i++) {
        val |= ((uint64_t)mem[addr + i]) << (i * 8);
    }
    return val;
}

void Memory::write64(uint64_t addr, uint64_t val) {
    // 边界检查：addr 必须 < MEM_SIZE - 7 才能写入 8 字节
    // 使用此检查避免 addr + 8 溢出的问题
    if (addr >= MEM_SIZE || addr > MEM_SIZE - 8) {
        throw std::runtime_error("Memory write out of bounds");
    }
    // 小端序写入
    for (int i = 0; i < 8; i++) {
        mem[addr + i] = (val >> (i * 8)) & 0xFF;
    }
}

void Memory::reset() {
    for (size_t i = 0; i < MEM_SIZE; i++) {
        mem[i] = 0;
    }
}

std::map<uint64_t, int64_t> Memory::getNonZeroMemory() const {
    std::map<uint64_t, int64_t> result;
    for (uint64_t addr = 0; addr < MEM_SIZE; addr += 8) {
        uint64_t val = read64(addr);
        if (val != 0) {
            // 将无符号值解释为有符号
            int64_t signed_val = static_cast<int64_t>(val);
            result[addr] = signed_val;
        }
    }
    return result;
}

