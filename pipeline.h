#ifndef PIPELINE_H
#define PIPELINE_H

#include "y86.h"
#include <cstdint>
#include <vector>

// 流水线寄存器结构
// F/D 寄存器：取指阶段输出，译码阶段输入
struct F_D_Register {
    bool valid = false;
    uint8_t icode = Y86::NOP;
    uint8_t ifun = 0;
    uint8_t rA = Y86::RNONE;
    uint8_t rB = Y86::RNONE;
    uint64_t valC = 0;      // 立即数或地址
    uint64_t valP = 0;      // 下一条指令的PC
    bool need_regids = false;
    bool need_valC = false;
    uint8_t stat = Y86::STAT_AOK;
};

// D/E 寄存器：译码阶段输出，执行阶段输入
struct D_E_Register {
    bool valid = false;
    bool is_bubble = false; // 是否是流水线插入的bubble（不应记录状态）
    uint8_t icode = Y86::NOP;
    uint8_t ifun = 0;
    uint64_t valA = 0;     // 源操作数A（可能被转发修改）
    uint64_t valB = 0;     // 源操作数B（可能被转发修改）
    uint64_t valC = 0;     // 立即数
    uint64_t valP = 0;     // 指令的下一条PC（用于状态记录）
    uint8_t dstE = Y86::RNONE;  // 目标寄存器E
    uint8_t dstM = Y86::RNONE;  // 目标寄存器M
    uint8_t srcA = Y86::RNONE;  // 源寄存器A
    uint8_t srcB = Y86::RNONE;  // 源寄存器B
    uint8_t stat = Y86::STAT_AOK;
};

// E/M 寄存器：执行阶段输出，访存阶段输入
struct E_M_Register {
    bool valid = false;
    bool is_bubble = false; // 是否是流水线插入的bubble（不应记录状态）
    uint8_t icode = Y86::NOP;
    uint64_t valE = 0;     // ALU计算结果
    uint64_t valA = 0;     // 用于访存的数据
    uint64_t valC = 0;     // 跳转目标地址（用于JXX和CALL）
    uint64_t valP = 0;     // 指令的下一条PC（用于状态记录）
    uint8_t dstE = Y86::RNONE;
    uint8_t dstM = Y86::RNONE;
    bool Cnd = false;      // 条件码判断结果
    bool set_cc = false;   // 是否设置条件码
    ConditionCodes CC;     // 新的条件码值（用于OPQ指令）
    uint8_t stat = Y86::STAT_AOK;
};

// M/W 寄存器：访存阶段输出，写回阶段输入
struct M_W_Register {
    bool valid = false;
    bool is_bubble = false; // 是否是流水线插入的bubble（不应记录状态）
    uint8_t icode = Y86::NOP;
    uint64_t valE = 0;     // ALU结果
    uint64_t valM = 0;     // 内存读取结果
    uint64_t valP = 0;     // 指令的下一条PC（用于状态记录）
    uint64_t valC = 0;     // 跳转目标地址（用于CALL和JXX）
    uint8_t dstE = Y86::RNONE;
    uint8_t dstM = Y86::RNONE;
    bool Cnd = false;      // 条件码判断结果（用于CMOVXX）
    bool set_cc = false;   // 是否设置条件码
    ConditionCodes CC;     // 新的条件码值（用于OPQ指令）
    uint8_t stat = Y86::STAT_AOK;
};

// 指令解析结果
struct Instruction {
    uint8_t icode;
    uint8_t ifun;
    uint8_t rA;
    uint8_t rB;
    uint64_t valC;
    uint64_t length;  // 指令长度（字节）
    uint8_t stat;
};

// 五级流水线模拟器
class PipelineSimulator {
public:
    PipelineSimulator();
    
    // 加载程序到内存
    void loadProgram(const std::vector<uint8_t>& program);
    
    // 运行模拟器
    void run();
    
    // 获取当前状态（用于输出JSON）
    struct State {
        uint64_t PC;
        RegisterFile regs;
        std::map<uint64_t, int64_t> mem_snapshot;  // 保存当时的非零内存值快照
        ConditionCodes CC;
        uint8_t STAT;
        
        State() {}
    };
    std::vector<State> getStates() const { return states_; }
    
    // 性能统计接口
    struct PerformanceStats {
        uint64_t total_cycles;      // 总周期数
        uint64_t instructions_retired;  // 已完成的指令数
        double ipc;                 // Instructions Per Cycle
        uint64_t stall_cycles;     // 停顿周期数（预留）
        uint64_t bubble_cycles;    // 气泡周期数（预留）
    };
    PerformanceStats getPerformanceStats() const {
        PerformanceStats stats;
        stats.total_cycles = cycle_count_;
        stats.instructions_retired = instruction_count_;
        stats.ipc = (cycle_count_ > 0) ? 
            static_cast<double>(instruction_count_) / cycle_count_ : 0.0;
        stats.stall_cycles = stall_cycles_;
        stats.bubble_cycles = bubble_cycles_;
        return stats;
    }
    
private:
    // 五个流水线阶段
    void fetch(F_D_Register& f_d);
    void decode(const F_D_Register& f_d, D_E_Register& d_e);
    void execute(const D_E_Register& d_e, E_M_Register& e_m);
    void memory(const E_M_Register& e_m, M_W_Register& m_w);
    void writeBack(const M_W_Register& m_w);
    
    // 冒险控制
    void applyForwarding(D_E_Register& d_e);
    bool needStall(const D_E_Register& d_e, const E_M_Register& e_m) const;
    bool needBubble(const D_E_Register& d_e, const E_M_Register& e_m) const;
    
    // 辅助函数
    Instruction parseInstruction(uint64_t pc) const;
    bool needRegids(uint8_t icode) const;
    bool needValC(uint8_t icode) const;
    uint64_t getPCNext(uint64_t pc, const Instruction& inst) const;
    
    // 条件码计算
    void setConditionCodes(uint8_t ifun, int64_t valA, int64_t valB, int64_t valE);
    bool getCondition(uint8_t ifun) const;
    
    // 状态记录
    void recordState(uint64_t instructionPC, const ConditionCodes& cc);
    
    // 处理器状态
    uint64_t PC_;
    RegisterFile regs_;
    Memory mem_;
    ConditionCodes CC_;
    uint8_t STAT_;
    
    // 流水线寄存器
    F_D_Register f_d_;
    D_E_Register d_e_;
    E_M_Register e_m_;
    M_W_Register m_w_;
    
    // 状态记录
    std::vector<State> states_;
    
    // 性能统计
    uint64_t cycle_count_;
    uint64_t instruction_count_;
    uint64_t stall_cycles_;      // Stall周期计数
    uint64_t bubble_cycles_;     // Bubble周期计数
    
    // 是否已停机
    bool halted_;
};

#endif // PIPELINE_H

