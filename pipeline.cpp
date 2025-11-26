#include "pipeline.h"
#include <iostream>
#include <stdexcept>
#include <algorithm>

PipelineSimulator::PipelineSimulator() 
    : PC_(0), STAT_(Y86::STAT_AOK), cycle_count_(0), instruction_count_(0), 
      stall_cycles_(0), bubble_cycles_(0), halted_(false) {
    // 初始条件码：ZF=1, SF=0, OF=0（根据CSAPP）
    CC_ = {true, false, false};
}

void PipelineSimulator::loadProgram(const std::vector<uint8_t>& program) {
    mem_.reset();
    regs_.reset();
    // program vector已经按照.yo文件中的绝对地址加载
    // 直接使用program的大小来确定内存范围
    for (size_t i = 0; i < program.size() && i < Memory::MEM_SIZE; i++) {
        mem_.mem[i] = program[i];
    }
    PC_ = 0;
    STAT_ = Y86::STAT_AOK;
    // 初始条件码：ZF=1, SF=0, OF=0（根据CSAPP Y86-64规范）
    CC_ = {true, false, false};
    states_.clear();
    cycle_count_ = 0;
    instruction_count_ = 0;
    stall_cycles_ = 0;
    bubble_cycles_ = 0;
    halted_ = false;
    
    // 初始化流水线寄存器
    f_d_.valid = false;
    d_e_.valid = false;
    e_m_.valid = false;
    m_w_.valid = false;
}

Instruction PipelineSimulator::parseInstruction(uint64_t pc) const {
    Instruction inst;
    inst.stat = Y86::STAT_AOK;
    
    if (pc >= Memory::MEM_SIZE) {
        inst.stat = Y86::STAT_ADR;
        return inst;
    }
    
    uint8_t byte1 = mem_.mem[pc];
    inst.icode = (byte1 >> 4) & 0xF;
    inst.ifun = byte1 & 0xF;
    inst.length = 1;
    
    // 检查非法指令（包括0xFF等）
    if (inst.icode > 0xB || inst.icode == 0xF) {
        inst.stat = Y86::STAT_INS;
        return inst;
    }
    
    // 需要寄存器ID的指令
    if (needRegids(inst.icode)) {
        if (pc + 1 >= Memory::MEM_SIZE) {
            inst.stat = Y86::STAT_ADR;
            return inst;
        }
        uint8_t byte2 = mem_.mem[pc + 1];
        inst.rA = (byte2 >> 4) & 0xF;
        inst.rB = byte2 & 0xF;
        inst.length = 2;
    } else {
        inst.rA = Y86::RNONE;
        inst.rB = Y86::RNONE;
    }
    
    // 需要立即数的指令
    if (needValC(inst.icode)) {
        if (pc + inst.length + 8 > Memory::MEM_SIZE) {
            inst.stat = Y86::STAT_ADR;
            return inst;
        }
        inst.valC = mem_.read64(pc + inst.length);
        inst.length += 8;
    } else {
        inst.valC = 0;
    }
    
    return inst;
}

bool PipelineSimulator::needRegids(uint8_t icode) const {
    return icode == Y86::RRMOVQ || icode == Y86::IRMOVQ || 
           icode == Y86::RMMOVQ || icode == Y86::MRMOVQ ||
           icode == Y86::OPQ || icode == Y86::PUSHQ || 
           icode == Y86::POPQ || icode == Y86::CMOVXX;
}

bool PipelineSimulator::needValC(uint8_t icode) const {
    return icode == Y86::IRMOVQ || icode == Y86::RMMOVQ || 
           icode == Y86::MRMOVQ || icode == Y86::JXX || 
           icode == Y86::CALL;
}

uint64_t PipelineSimulator::getPCNext(uint64_t pc, const Instruction& inst) const {
    return pc + inst.length;
}

// Fetch 阶段
void PipelineSimulator::fetch(F_D_Register& f_d) {
    if (STAT_ != Y86::STAT_AOK) {
        f_d.valid = false;
        f_d.stat = STAT_;
        return;
    }
    
    Instruction inst = parseInstruction(PC_);
    
    f_d.icode = inst.icode;
    f_d.ifun = inst.ifun;
    f_d.rA = inst.rA;
    f_d.rB = inst.rB;
    f_d.valC = inst.valC;
    f_d.valP = getPCNext(PC_, inst);
    f_d.need_regids = needRegids(inst.icode);
    f_d.need_valC = needValC(inst.icode);
    f_d.stat = inst.stat;
    f_d.valid = (inst.stat == Y86::STAT_AOK);
    
    // 更新PC（预测下一条指令地址）
    // 对于JXX，预测不跳转（使用valP），如果预测失败会在execute阶段修正
    // 对于CALL，总是跳转（使用valC）
    // 对于RET，预测下一条PC（但实际PC会在writeBack阶段后更新）
    // 对于HALT，PC不再更新（因为程序即将结束）
    if (inst.icode == Y86::CALL) {
        PC_ = inst.valC;  // CALL总是跳转
    } else if (inst.icode == Y86::JXX) {
        PC_ = f_d.valP;   // JXX预测不跳转
    } else if (inst.icode == Y86::RET) {
        // RET指令：PC不在这里更新，会在M阶段后更新
        // 保持PC不变，等待M阶段后更新
    } else if (inst.icode == Y86::HALT) {
        // HALT指令：PC不再更新，停止取指
        // 不修改PC，后续周期不会再fetch新指令
    } else {
        PC_ = f_d.valP;   // 其他指令使用下一条PC
    }
}

// Decode 阶段
void PipelineSimulator::decode(const F_D_Register& f_d, D_E_Register& d_e) {
    d_e.icode = f_d.icode;
    d_e.ifun = f_d.ifun;
    d_e.valC = f_d.valC;
    d_e.valP = f_d.valP;  // 保存下一条PC
    d_e.stat = f_d.stat;
    d_e.valid = f_d.valid;
    d_e.is_bubble = false;  // 正常指令不是bubble
    
    // 确定源寄存器和目标寄存器
    uint8_t icode = f_d.icode;
    
    // srcA
    if (icode == Y86::RRMOVQ || icode == Y86::RMMOVQ || 
        icode == Y86::OPQ || icode == Y86::PUSHQ) {
        d_e.srcA = f_d.rA;
    } else if (icode == Y86::POPQ || icode == Y86::RET) {
        d_e.srcA = Y86::RSP;
    } else {
        d_e.srcA = Y86::RNONE;
    }
    
    // srcB
    if (icode == Y86::OPQ || icode == Y86::RMMOVQ || icode == Y86::MRMOVQ) {
        d_e.srcB = f_d.rB;
    } else if (icode == Y86::PUSHQ || icode == Y86::POPQ || 
               icode == Y86::CALL || icode == Y86::RET) {
        d_e.srcB = Y86::RSP;
    } else {
        d_e.srcB = Y86::RNONE;
    }
    
    // dstE
    if (icode == Y86::IRMOVQ) {
        d_e.dstE = f_d.rB;
    } else if (icode == Y86::RRMOVQ || icode == Y86::CMOVXX) {
        d_e.dstE = f_d.rB;
    } else if (icode == Y86::OPQ) {
        d_e.dstE = f_d.rB;
    } else if (icode == Y86::PUSHQ || icode == Y86::POPQ || 
               icode == Y86::CALL || icode == Y86::RET) {
        d_e.dstE = Y86::RSP;
    } else {
        d_e.dstE = Y86::RNONE;
    }
    
    // dstM
    if (icode == Y86::MRMOVQ || icode == Y86::POPQ) {
        d_e.dstM = f_d.rA;
    } else {
        d_e.dstM = Y86::RNONE;
    }
    
    // 读取寄存器值（转发逻辑会修改这些值）
    d_e.valA = regs_.get(d_e.srcA);
    d_e.valB = regs_.get(d_e.srcB);
    
    // 特殊处理：PUSHQ、POPQ、CALL的valA
    if (icode == Y86::PUSHQ) {
        d_e.valA = regs_.get(f_d.rA);
    } else if (icode == Y86::POPQ || icode == Y86::RET) {
        d_e.valA = regs_.get(Y86::RSP);
    } else if (icode == Y86::CALL) {
        d_e.valA = f_d.valP;  // 返回地址（下一条指令的PC）
    }
}

// Execute 阶段
void PipelineSimulator::execute(const D_E_Register& d_e, E_M_Register& e_m) {
    e_m.icode = d_e.icode;
    e_m.dstE = d_e.dstE;
    e_m.dstM = d_e.dstM;
    e_m.valA = d_e.valA;
    e_m.valC = d_e.valC;  // 保存跳转目标地址
    e_m.valP = d_e.valP;  // 保存下一条PC
    e_m.stat = d_e.stat;
    e_m.valid = d_e.valid;
    e_m.is_bubble = d_e.is_bubble;  // 传递bubble标志
    
    uint8_t icode = d_e.icode;
    uint8_t ifun = d_e.ifun;
    
    // ALU操作
    if (icode == Y86::OPQ) {
        int64_t valA = static_cast<int64_t>(d_e.valA);
        int64_t valB = static_cast<int64_t>(d_e.valB);
        int64_t valE = 0;
        
        switch (ifun) {
            case Y86::ADD:
                valE = valA + valB;
                break;
            case Y86::SUB:
                valE = valB - valA;  // subq %rA,%rB: rB = rB - rA
                break;
            case Y86::AND:
                valE = valA & valB;
                break;
            case Y86::XOR:
                valE = valA ^ valB;
                break;
        }
        
        // 计算新的条件码
        e_m.set_cc = true;
        e_m.CC.ZF = (valE == 0);
        e_m.CC.SF = (valE < 0);
        // 溢出检测
        // 对于 valE = valB - valA:
        //   overflow if (valB > 0 && valA < 0 && valE < 0): positive - negative -> should be positive, overflow if negative
        //   overflow if (valB < 0 && valA > 0 && valE > 0): negative - positive -> should be negative, overflow if positive
        bool overflow = false;
        if (ifun == Y86::ADD) {
            overflow = ((valA > 0 && valB > 0 && valE < 0) ||
                       (valA < 0 && valB < 0 && valE > 0));
        } else if (ifun == Y86::SUB) {
            overflow = ((valA < 0 && valB > 0 && valE < 0) ||
                       (valA > 0 && valB < 0 && valE > 0));
        }
        e_m.CC.OF = overflow;
        
        // 同时更新全局CC_（用于后续的条件判断）
        setConditionCodes(ifun, valA, valB, valE);
        
        e_m.valE = static_cast<uint64_t>(valE);
        
    } else if (icode == Y86::IRMOVQ) {
        e_m.valE = d_e.valC;  // IRMOVQ: valC -> valE
        e_m.Cnd = true;  // 无条件移动
        
    } else if (icode == Y86::RRMOVQ) {  // RRMOVQ和CMOVXX都是icode=2
        e_m.valE = d_e.valA;  // 源寄存器的值
        if (ifun == 0) {
            // rrmovq: 无条件移动
            e_m.Cnd = true;
        } else {
            // cmovxx: 条件移动，根据ifun判断条件
            e_m.Cnd = getCondition(ifun);
        }
        
    } else if (icode == Y86::RMMOVQ || icode == Y86::MRMOVQ) {
        e_m.valE = d_e.valB + d_e.valC;  // 计算有效地址
        
    } else if (icode == Y86::PUSHQ || icode == Y86::CALL) {
        e_m.valE = d_e.valB - 8;  // RSP - 8
        
    } else if (icode == Y86::POPQ || icode == Y86::RET) {
        e_m.valE = d_e.valB + 8;  // RSP + 8
        
    } else if (icode == Y86::JXX) {
        e_m.Cnd = getCondition(ifun);
        // PC更新在主循环中根据Cnd处理
        
    } else if (icode == Y86::CALL) {
        e_m.valE = d_e.valB - 8;  // RSP - 8（已在上面处理）
        e_m.Cnd = true;  // CALL总是执行
        
    } else {
        e_m.valE = 0;
        e_m.Cnd = false;
    }
    
    // 对于非OPQ指令，保存当前的CC值（用于状态记录）
    if (icode != Y86::OPQ) {
        e_m.set_cc = false;
        e_m.CC = CC_;  // 保存进入Execute阶段时的CC值
    }
}

// Memory 阶段
void PipelineSimulator::memory(const E_M_Register& e_m, M_W_Register& m_w) {
    m_w.icode = e_m.icode;
    m_w.valE = e_m.valE;
    m_w.valP = e_m.valP;  // 保存下一条PC
    m_w.valC = e_m.valC;  // 保存跳转目标地址（用于CALL和JXX）
    m_w.dstE = e_m.dstE;
    m_w.dstM = e_m.dstM;
    m_w.Cnd = e_m.Cnd;    // 保存条件判断结果
    m_w.set_cc = e_m.set_cc;  // 传递条件码设置标志
    m_w.CC = e_m.CC;          // 传递新的条件码值
    m_w.stat = e_m.stat;
    m_w.valid = e_m.valid;
    m_w.is_bubble = e_m.is_bubble;  // 传递bubble标志
    
    uint8_t icode = e_m.icode;
    
    if (icode == Y86::MRMOVQ) {
        // 从内存读取
        try {
            m_w.valM = mem_.read64(e_m.valE);
        } catch (...) {
            m_w.stat = Y86::STAT_ADR;
        }
    } else if (icode == Y86::POPQ || icode == Y86::RET) {
        // 从栈读取（使用旧的RSP值，即valA）
        try {
            m_w.valM = mem_.read64(e_m.valA);  // 使用旧的RSP值（在decode阶段读取）
            // RET指令：在M阶段结束时立即更新PC，并设置flush信号
            if (icode == Y86::RET && m_w.stat == Y86::STAT_AOK) {
                PC_ = m_w.valM;  // 立即更新PC为返回地址
            }
        } catch (...) {
            m_w.stat = Y86::STAT_ADR;
        }
    } else {
        m_w.valM = 0;
    }
    
    if (icode == Y86::RMMOVQ || icode == Y86::PUSHQ) {
        // 写入内存
        try {
            mem_.write64(e_m.valE, e_m.valA);
        } catch (...) {
            m_w.stat = Y86::STAT_ADR;
        }
    } else if (icode == Y86::CALL) {
        // 将返回地址压栈
        try {
            mem_.write64(e_m.valE, e_m.valA);  // valA包含返回地址
        } catch (...) {
            m_w.stat = Y86::STAT_ADR;
        }
    }
}

// WriteBack 阶段
void PipelineSimulator::writeBack(const M_W_Register& m_w) {
    // 如果已经停机，不再处理任何指令（HALT之后的气泡）
    if (halted_) {
        return;
    }
    
    // 如果是bubble，跳过（不记录状态）
    if (m_w.is_bubble) {
        return;
    }
    
    if (m_w.stat != Y86::STAT_AOK) {
        // 即使出错，也要完成寄存器写回（因为计算已经完成，只是内存访问失败）
        // 例如 pushq: RSP已经被计算为 RSP-8，即使内存写入失败，RSP仍应更新
        if (m_w.dstE != Y86::RNONE) {
            regs_.set(m_w.dstE, static_cast<int64_t>(m_w.valE));
        }
        // dstM不应该更新，因为内存读取失败
        
        STAT_ = m_w.stat;
        // 记录错误状态
        // PC使用valP - 指令长度（回到指令本身的地址）
        // 对于pushq（0xa0），它是2字节指令，所以PC应该是valP - 2
        uint64_t error_pc = m_w.valP - 2;  // 假设是2字节指令
        recordState(error_pc, m_w.CC);
        return;
    }
    
    // 使用流水线寄存器中保存的CC值来记录状态
    // - OPQ指令: set_cc=true, 使用自己计算的新CC值
    // - 非OPQ指令: set_cc=false, 使用进入Execute阶段时保存的CC值
    ConditionCodes cc_for_record = m_w.CC;
    
    uint8_t icode = m_w.icode;
    
    // 写回寄存器E
    if (m_w.dstE != Y86::RNONE) {
        if (icode == Y86::CMOVXX) {
            // 条件移动：只有当条件满足时才写回
            if (m_w.Cnd) {
                regs_.set(m_w.dstE, static_cast<int64_t>(m_w.valE));
            }
        } else {
            // 其他指令：无条件写回
            regs_.set(m_w.dstE, static_cast<int64_t>(m_w.valE));
        }
    }
    
    // 写回寄存器M
    if (m_w.dstM != Y86::RNONE) {
        regs_.set(m_w.dstM, static_cast<int64_t>(m_w.valM));
    }
    
    // 统计完成的指令
    instruction_count_++;
    
    // 检查停机（在记录状态之前设置STAT，这样HALT记录的状态就是STAT=2）
    if (icode == Y86::HALT) {
        STAT_ = Y86::STAT_HLT;
        halted_ = true;
    }
    
    // 记录状态（每条指令完成后，包括NOP）
    // 对于CALL指令，使用跳转目标地址（valC）
    // 对于JXX指令（跳转成功），使用跳转目标地址（valC）
    // 对于HALT指令，使用halt指令本身的地址（valP - 1）
    // 对于其他指令，使用valP（指令的下一条PC）
    uint64_t pc_to_record;
    if (icode == Y86::CALL) {
        // CALL指令：使用跳转目标地址（valC）
        pc_to_record = m_w.valC;
    } else if (icode == Y86::JXX && m_w.Cnd) {
        // JXX指令（跳转成功）：使用跳转目标地址（valC）
        pc_to_record = m_w.valC;
    } else if (icode == Y86::RET) {
        // RET指令：使用返回地址（valM，从栈中读取的地址）
        pc_to_record = m_w.valM;
    } else if (icode == Y86::HALT) {
        // HALT指令：使用halt指令本身的地址（halt长度为1，所以是 valP - 1）
        pc_to_record = m_w.valP - 1;
    } else {
        // 其他指令（包括NOP）：使用valP（指令的下一条PC）
        pc_to_record = m_w.valP;
    }
    recordState(pc_to_record, cc_for_record);
}

// 数据转发
void PipelineSimulator::applyForwarding(D_E_Register& d_e) {
    // 转发源A
    if (d_e.srcA != Y86::RNONE) {
        // 从E/M阶段转发（注意：CMOVXX with Cnd=false 不应该转发dstE）
        bool e_m_can_forward_E = (e_m_.dstE == d_e.srcA && e_m_.dstE != Y86::RNONE);
        // 对于CMOVXX（icode=2, ifun!=0），只有当Cnd=true时才转发
        if (e_m_.icode == Y86::RRMOVQ && e_m_.valid) {
            // RRMOVQ/CMOVXX: ifun=0 是 rrmovq（总是转发），ifun!=0 是 cmovxx（只在Cnd=true时转发）
            // 但我们在这里没有ifun信息，需要用Cnd来判断
            // 如果Cnd=false且是条件移动，不应该转发
            if (!e_m_.Cnd) {
                e_m_can_forward_E = false;
            }
        }
        if (e_m_can_forward_E) {
            d_e.valA = e_m_.valE;
        }
        // 从M/W阶段转发（同样需要检查CMOVXX）
        else {
            bool m_w_can_forward_E = (m_w_.dstE == d_e.srcA && m_w_.dstE != Y86::RNONE);
            if (m_w_.icode == Y86::RRMOVQ && m_w_.valid) {
                if (!m_w_.Cnd) {
                    m_w_can_forward_E = false;
                }
            }
            if (m_w_can_forward_E) {
                d_e.valA = m_w_.valE;
            }
            else if (m_w_.dstM == d_e.srcA && m_w_.dstM != Y86::RNONE) {
                d_e.valA = m_w_.valM;
            }
        }
    }
    
    // 转发源B
    if (d_e.srcB != Y86::RNONE) {
        // 从E/M阶段转发
        bool e_m_can_forward_E = (e_m_.dstE == d_e.srcB && e_m_.dstE != Y86::RNONE);
        if (e_m_.icode == Y86::RRMOVQ && e_m_.valid) {
            if (!e_m_.Cnd) {
                e_m_can_forward_E = false;
            }
        }
        if (e_m_can_forward_E) {
            d_e.valB = e_m_.valE;
        }
        // 从M/W阶段转发
        else {
            bool m_w_can_forward_E = (m_w_.dstE == d_e.srcB && m_w_.dstE != Y86::RNONE);
            if (m_w_.icode == Y86::RRMOVQ && m_w_.valid) {
                if (!m_w_.Cnd) {
                    m_w_can_forward_E = false;
                }
            }
            if (m_w_can_forward_E) {
                d_e.valB = m_w_.valE;
            }
            else if (m_w_.dstM == d_e.srcB && m_w_.dstM != Y86::RNONE) {
                d_e.valB = m_w_.valM;
            }
        }
    }
}

// 检查是否需要停顿（Load/Use Hazard）
bool PipelineSimulator::needStall(const D_E_Register& d_e, const E_M_Register& e_m) const {
    // Load/Use Hazard: E/M阶段的指令（MRMOVQ或POPQ）从内存读取数据
    // D/E阶段的指令需要使用这个数据，但数据还没准备好
    if (e_m.icode == Y86::MRMOVQ || e_m.icode == Y86::POPQ) {
        uint8_t dstM = e_m.dstM;
        if (dstM != Y86::RNONE && d_e.valid) {
            // 检查D/E阶段指令是否需要这个寄存器
            uint8_t icode = d_e.icode;
            // 检查srcA
            if (d_e.srcA == dstM) {
                // 需要源寄存器A
                if (icode == Y86::RRMOVQ || icode == Y86::RMMOVQ || 
                    icode == Y86::OPQ || icode == Y86::PUSHQ || icode == Y86::CMOVXX) {
                    return true;
                }
            }
            // 检查srcB
            if (d_e.srcB == dstM) {
                // 需要源寄存器B
                if (icode == Y86::OPQ || icode == Y86::RMMOVQ || icode == Y86::MRMOVQ) {
                    return true;
                }
            }
            // RET指令总是需要RSP
            if (icode == Y86::RET && dstM == Y86::RSP) return true;
        }
    }
    return false;
}

// 检查是否需要气泡（控制冒险）
bool PipelineSimulator::needBubble(const D_E_Register& d_e, const E_M_Register& e_m) const {
    // 在我的实现中，JXX预测不跳转（使用valP）
    // 如果Cnd=true（实际跳转），预测失败，需要在主循环中通过jmp_flush处理
    // 如果Cnd=false（不跳转），预测正确，不需要bubble
    // 所以这里不需要为JXX返回bubble，JXX的控制冒险在主循环中通过jmp_flush处理
    // RET指令不需要气泡，因为PC更新在M阶段处理，flush也在主循环中处理
    return false;
}

// 设置条件码
void PipelineSimulator::setConditionCodes(uint8_t ifun, int64_t valA, int64_t valB, int64_t valE) {
    CC_.ZF = (valE == 0);
    CC_.SF = (valE < 0);
    
    // 溢出检测
    // 对于 valE = valB - valA:
    //   overflow if (valB > 0 && valA < 0 && valE < 0): positive - negative -> should be positive, overflow if negative
    //   overflow if (valB < 0 && valA > 0 && valE > 0): negative - positive -> should be negative, overflow if positive
    bool overflow = false;
    switch (ifun) {
        case Y86::ADD:
            overflow = ((valA > 0 && valB > 0 && valE < 0) ||
                       (valA < 0 && valB < 0 && valE > 0));
            break;
        case Y86::SUB:
            overflow = ((valA < 0 && valB > 0 && valE < 0) ||
                       (valA > 0 && valB < 0 && valE > 0));
            break;
    }
    CC_.OF = overflow;
}

// 获取条件判断结果
bool PipelineSimulator::getCondition(uint8_t ifun) const {
    switch (ifun) {
        case Y86::C_YES: return true;
        case Y86::C_LE: return ((CC_.SF || CC_.ZF) && !CC_.OF) || ((!CC_.SF && !CC_.ZF) && CC_.OF);
        case Y86::C_L: return CC_.SF != CC_.OF;
        case Y86::C_E: return CC_.ZF;
        case Y86::C_NE: return !CC_.ZF;
        case Y86::C_GE: return CC_.SF == CC_.OF;
        case Y86::C_G: return !CC_.ZF && (CC_.SF == CC_.OF);
        default: return false;
    }
}

// 记录状态（使用指令完成时的PC和条件码）
void PipelineSimulator::recordState(uint64_t instructionPC, const ConditionCodes& cc) {
    State state;
    state.PC = instructionPC;  // 使用指令完成时的PC
    state.regs = regs_;
    state.mem_snapshot = mem_.getNonZeroMemory();  // 保存当时的内存快照
    state.CC = cc;
    state.STAT = STAT_;
    states_.push_back(state);
}

// 主运行循环
void PipelineSimulator::run() {
    // 循环条件：STAT正常且未停机，或者已停机但流水线还未排空
    while ((STAT_ == Y86::STAT_AOK && !halted_) || 
           (halted_ && (f_d_.valid || d_e_.valid || e_m_.valid || m_w_.valid))) {
        cycle_count_++;
        
        // 从后往前执行（W -> M -> E -> D -> F）
        // 保存当前周期的流水线寄存器状态（用于冒险检测和控制流）
        M_W_Register m_w_prev = m_w_;
        E_M_Register e_m_prev = e_m_;
        D_E_Register d_e_prev = d_e_;
        F_D_Register f_d_prev = f_d_;
        
        // 创建新的流水线寄存器（用于下一个周期）
        M_W_Register m_w_new = m_w_;
        E_M_Register e_m_new = e_m_;
        D_E_Register d_e_new = d_e_;
        F_D_Register f_d_new = f_d_;
        
        // 1. WriteBack阶段（先执行，记录当前完成指令的状态）
        if (m_w_.valid) {
            writeBack(m_w_);
        }
        
        // 2. Memory阶段
        if (e_m_prev.valid) {
            memory(e_m_prev, m_w_new);
        } else {
            m_w_new.valid = false;
        }
        
        // 5. 检查冒险（在execute之前检查，使用执行前的状态）
        bool stall = needStall(d_e_prev, e_m_prev);
        bool bubble = needBubble(d_e_prev, e_m_prev);
        
        // 统计Stall周期
        if (stall) {
            stall_cycles_++;
        }
        
        // 处理跳转和控制流 - 暂时不检查，会在execute之后检查
        bool jmp_flush = false;  // JXX跳转成功需要flush
        
        // RET指令处理（PC已在M阶段更新，这里只需要处理flush）
        // RET指令在M阶段结束时已经更新了PC，现在需要flush流水线
        // 检查m_w_new（当前周期memory阶段的结果）是否包含RET指令
        bool ret_flush = false;
        if (m_w_new.valid && m_w_new.icode == Y86::RET && m_w_new.stat == Y86::STAT_AOK) {
            // RET指令刚刚完成M阶段，需要flush F/D、D/E、E/M三个阶段
            ret_flush = true;
        }
        
        // Execute 阶段：执行 d_e_prev 中的指令
        // 首先对 d_e_prev 应用转发（从 e_m_prev 和 m_w_prev 获取最新数据）
        D_E_Register d_e_for_execute = d_e_prev;
        if (d_e_for_execute.valid && !stall) {
            // 临时保存e_m_和m_w_，用于转发
            E_M_Register e_m_temp = e_m_;
            M_W_Register m_w_temp = m_w_;
            e_m_ = e_m_prev;
            m_w_ = m_w_prev;
            applyForwarding(d_e_for_execute);
            e_m_ = e_m_temp;
            m_w_ = m_w_temp;
            
            // 执行
            execute(d_e_for_execute, e_m_new);
        } else if (stall) {
            // Load/Use Hazard stall: 在E/M阶段插入bubble
            e_m_new.icode = Y86::NOP;
            e_m_new.valA = 0;
            e_m_new.valC = 0;
            e_m_new.valE = 0;
            e_m_new.valP = 0;
            e_m_new.dstE = Y86::RNONE;
            e_m_new.dstM = Y86::RNONE;
            e_m_new.Cnd = false;
            e_m_new.set_cc = false;
            e_m_new.stat = Y86::STAT_AOK;
            e_m_new.valid = true;
            e_m_new.is_bubble = true;  // 标记为bubble
        } else {
            e_m_new.valid = false;
        }
        
        // 处理跳转和控制流（在Execute阶段之后检测）
        // 使用e_m_new（刚刚执行的指令）而不是e_m_prev
        if (e_m_new.valid && e_m_new.icode == Y86::JXX) {
            if (e_m_new.Cnd) {
                // 跳转预测失败（预测不跳转但实际跳转），需要跳转
                PC_ = e_m_new.valC;  // 跳转目标地址
                // 设置flush标志，清空F/D和D/E阶段
                jmp_flush = true;
                f_d_new.valid = false;
            }
            // 如果Cnd=false，预测正确（预测不跳转且实际不跳转），PC已经在fetch阶段设置为valP
        }
        
        // Decode 阶段：处理 f_d_prev，生成 d_e_new
        if (stall) {
            // Load/Use Hazard stall: D/E寄存器保持不变
            // 但需要重新读取寄存器值，因为 writeBack 可能已经更新了寄存器
            d_e_new = d_e_prev;
            // 重新读取寄存器值（这样可以获取 stall 周期 writeBack 写入的最新值）
            if (d_e_new.srcA != Y86::RNONE) {
                d_e_new.valA = regs_.get(d_e_new.srcA);
            }
            if (d_e_new.srcB != Y86::RNONE) {
                d_e_new.valB = regs_.get(d_e_new.srcB);
            }
        } else if (bubble || ret_flush || jmp_flush) {
            // 注入气泡（NOP）- 用于控制冒险、RET指令flush或JXX跳转flush
            d_e_new.icode = Y86::NOP;
            d_e_new.ifun = 0;
            d_e_new.valA = 0;
            d_e_new.valB = 0;
            d_e_new.valC = 0;
            d_e_new.valP = 0;
            d_e_new.dstE = Y86::RNONE;
            d_e_new.dstM = Y86::RNONE;
            d_e_new.srcA = Y86::RNONE;
            d_e_new.srcB = Y86::RNONE;
            d_e_new.stat = Y86::STAT_AOK;
            d_e_new.valid = true;
            d_e_new.is_bubble = true;  // 标记为bubble
            
            // 统计Bubble周期（根据实际浪费的周期数）
            // ret_flush: 3 cycles wasted (flush F/D, D/E, E/M)
            // jmp_flush: 2 cycles wasted (flush F/D, D/E)
            // plain bubble: 1 cycle wasted
            if (ret_flush) {
                bubble_cycles_ += 3;
            } else if (jmp_flush) {
                bubble_cycles_ += 2;
            } else {
                bubble_cycles_ += 1;
            }
        } else if (f_d_prev.valid) {
            decode(f_d_prev, d_e_new);
        } else {
            // 如果f_d_无效，清空d_e_
            d_e_new.valid = false;
        }
        
        // RET指令flush：如果RET指令在M阶段，需要flush E/M阶段（注入bubble）
        if (ret_flush) {
            e_m_new.icode = Y86::NOP;
            e_m_new.valA = 0;
            e_m_new.valC = 0;
            e_m_new.valE = 0;
            e_m_new.valP = 0;
            e_m_new.dstE = Y86::RNONE;
            e_m_new.dstM = Y86::RNONE;
            e_m_new.Cnd = false;
            e_m_new.stat = Y86::STAT_AOK;
            e_m_new.valid = true;
            e_m_new.is_bubble = true;  // 标记为bubble
        }
        
        // 7. Fetch阶段（如果不停顿）
        // 检查是否已经fetch过HALT指令（流水线中有HALT就不再fetch）
        bool halt_in_pipeline = (f_d_prev.valid && f_d_prev.icode == Y86::HALT) ||
                                (d_e_prev.valid && d_e_prev.icode == Y86::HALT) ||
                                (e_m_prev.valid && e_m_prev.icode == Y86::HALT) ||
                                (m_w_prev.valid && m_w_prev.icode == Y86::HALT);
        if (!stall) {
            if (ret_flush || jmp_flush || halt_in_pipeline) {
                // RET或JXX跳转flush，或HALT在流水线中：不再fetch
                f_d_new.valid = false;
            } else {
                fetch(f_d_new);
            }
        } else {
            // 如果stall，保持f_d_不变（不更新）
            f_d_new = f_d_prev;
        }
        
        // 更新流水线寄存器：将新周期的值赋给当前寄存器
        m_w_ = m_w_new;
        e_m_ = e_m_new;
        d_e_ = d_e_new;
        f_d_ = f_d_new;
        
        // 检查是否所有阶段都为空且已停机（流水线排空）
        // 注意：halt指令在writeBack阶段设置halted_标志，但需要等待流水线排空
        if (halted_ || STAT_ != Y86::STAT_AOK) {
            // 如果已停机，等待流水线排空（所有阶段都无效）
            if (!f_d_.valid && !d_e_.valid && !e_m_.valid && !m_w_.valid) {
                // 如果STAT_=STAT_HLT，需要记录halt完成状态（STAT=2）
                // 但只有在还没有记录过halt完成状态时才记录
                if (STAT_ == Y86::STAT_HLT && !states_.empty() && states_.back().STAT == Y86::STAT_AOK) {
                    // 使用最后一个状态的PC（halt指令的PC）
                    recordState(states_.back().PC, CC_);
                }
                // 流水线已排空，退出循环
                break;
            }
        }
        
        // 安全检查：防止无限循环
        if (cycle_count_ > 1000000) {
            STAT_ = Y86::STAT_INS;
            break;
        }
    }
    
    // 确保halt指令的状态被记录
    if (halted_ && m_w_.valid && m_w_.icode == Y86::HALT) {
        // halt指令的状态已经在writeBack阶段记录，这里不需要额外处理
    }
}

