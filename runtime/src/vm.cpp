#include "alkv/vm.hpp"
#include <cmath>
#include <stdexcept>

namespace alkv::vm {

static bool valueEquals(const Value& a, const Value& b) {
    if (a.tag != b.tag) return false;

    switch (a.tag) {
        case ValueTag::Nil:   return true;
        case ValueTag::Int:   return a.as.i == b.as.i;
        case ValueTag::Float: return a.as.f == b.as.f;
        case ValueTag::Bool:  return a.as.b == b.as.b;
        case ValueTag::Obj: {
            if (a.as.obj == b.as.obj) return true;
            if (!a.as.obj || !b.as.obj) return false;
            if (a.as.obj->type != b.as.obj->type) return false;

            // Сейчас в heap есть только строки
            if (a.as.obj->type == ObjType::String) {
                auto* sa = static_cast<ObjString*>(a.as.obj);
                auto* sb = static_cast<ObjString*>(b.as.obj);
                return sa->view() == sb->view();
            }
            return false;
        }
    }
    return false;
}

int32_t VM::asInt(const Value& v) {
    if (v.tag != ValueTag::Int) throw std::runtime_error("TypeError: expected int");
    return v.as.i;
}

float VM::asFloat(const Value& v) {
    if (v.tag != ValueTag::Float) throw std::runtime_error("TypeError: expected float");
    return v.as.f;
}

bool VM::asBool(const Value& v) {
    if (v.tag != ValueTag::Bool) throw std::runtime_error("TypeError: expected bool");
    return v.as.b;
}

Value VM::run(const bc::Function& fn, const std::vector<Value>& args) {
    // 1) загрузить runtime-constpool
    mem.constPool = fn.constPool;

    // 2) выделить фрейм на regCount регистров
    mem.pushFrame(fn.regCount);

    // 3) положить аргументы в R0.. (convention)
    if (args.size() > fn.regCount) {
        mem.popFrame();
        throw std::runtime_error("Too many args for regCount");
    }
    for (size_t i = 0; i < args.size(); ++i) {
        mem.reg(static_cast<uint16_t>(i)) = args[i];
    }

    const auto& code = fn.code;
    int32_t pc = 0;

    // 4) интерпретатор
    while (pc >= 0 && pc < static_cast<int32_t>(code.size())) {
        uint32_t w = code[pc];
        bc::Opcode op = bc::decodeOp(w);

        switch (op) {
            case bc::Opcode::NOP: {
                pc++;
                break;
            }

            case bc::Opcode::MOV: {
                auto d = bc::decodeABC(w);
                mem.reg(d.a) = mem.reg(d.b);
                pc++;
                break;
            }

            case bc::Opcode::LOADK: {
                auto d = bc::decodeABx(w);
                if (d.bx >= mem.constPool.size()) {
                    mem.popFrame();
                    throw std::runtime_error("LOADK: const index out of bounds");
                }
                mem.reg(d.a) = mem.constPool[d.bx];
                pc++;
                break;
            }

            // -------- int arith --------
            case bc::Opcode::ADD_I:
            case bc::Opcode::SUB_I:
            case bc::Opcode::MUL_I:
            case bc::Opcode::DIV_I:
            case bc::Opcode::MOD_I: {
                auto d = bc::decodeABC(w);
                int32_t lhs = asInt(mem.reg(d.b));
                int32_t rhs = asInt(mem.reg(d.c));
                int32_t res = 0;
                switch (op) {
                    case bc::Opcode::ADD_I: res = lhs + rhs; break;
                    case bc::Opcode::SUB_I: res = lhs - rhs; break;
                    case bc::Opcode::MUL_I: res = lhs * rhs; break;
                    case bc::Opcode::DIV_I: res = lhs / rhs; break;
                    case bc::Opcode::MOD_I: res = lhs % rhs; break;
                    default: break;
                }
                mem.reg(d.a) = Value::i32(res);
                pc++;
                break;
            }

            // -------- float arith --------
            case bc::Opcode::ADD_F:
            case bc::Opcode::SUB_F:
            case bc::Opcode::MUL_F:
            case bc::Opcode::DIV_F:
            case bc::Opcode::MOD_F: {
                auto d = bc::decodeABC(w);
                float lhs = asFloat(mem.reg(d.b));
                float rhs = asFloat(mem.reg(d.c));
                float res = 0.0f;
                switch (op) {
                    case bc::Opcode::ADD_F: res = lhs + rhs; break;
                    case bc::Opcode::SUB_F: res = lhs - rhs; break;
                    case bc::Opcode::MUL_F: res = lhs * rhs; break;
                    case bc::Opcode::DIV_F: res = lhs / rhs; break;
                    case bc::Opcode::MOD_F: res = std::fmod(lhs, rhs); break;
                    default: break;
                }
                mem.reg(d.a) = Value::f32(res);
                pc++;
                break;
            }

            // -------- int comparisons -> bool --------
            case bc::Opcode::LT_I: case bc::Opcode::LE_I:
            case bc::Opcode::GT_I: case bc::Opcode::GE_I: {
                auto d = bc::decodeABC(w);
                int32_t lhs = asInt(mem.reg(d.b));
                int32_t rhs = asInt(mem.reg(d.c));
                bool res = false;
                switch (op) {
                    case bc::Opcode::LT_I: res = lhs <  rhs; break;
                    case bc::Opcode::LE_I: res = lhs <= rhs; break;
                    case bc::Opcode::GT_I: res = lhs >  rhs; break;
                    case bc::Opcode::GE_I: res = lhs >= rhs; break;
                    default: break;
                }
                mem.reg(d.a) = Value::boolean(res);
                pc++;
                break;
            }

            // -------- float comparisons -> bool --------
            case bc::Opcode::LT_F: case bc::Opcode::LE_F:
            case bc::Opcode::GT_F: case bc::Opcode::GE_F: {
                auto d = bc::decodeABC(w);
                float lhs = asFloat(mem.reg(d.b));
                float rhs = asFloat(mem.reg(d.c));
                bool res = false;
                switch (op) {
                    case bc::Opcode::LT_F: res = lhs <  rhs; break;
                    case bc::Opcode::LE_F: res = lhs <= rhs; break;
                    case bc::Opcode::GT_F: res = lhs >  rhs; break;
                    case bc::Opcode::GE_F: res = lhs >= rhs; break;
                    default: break;
                }
                mem.reg(d.a) = Value::boolean(res);
                pc++;
                break;
            }

            // -------- equality --------
            case bc::Opcode::EQ:
            case bc::Opcode::NE: {
                auto d = bc::decodeABC(w);
                bool eq = valueEquals(mem.reg(d.b), mem.reg(d.c));
                mem.reg(d.a) = Value::boolean(op == bc::Opcode::EQ ? eq : !eq);
                pc++;
                break;
            }

            // -------- boolean ops --------
            case bc::Opcode::NOT: {
                auto d = bc::decodeABC(w);
                bool v = asBool(mem.reg(d.b));
                mem.reg(d.a) = Value::boolean(!v);
                pc++;
                break;
            }

            case bc::Opcode::AND:
            case bc::Opcode::OR: {
                auto d = bc::decodeABC(w);
                bool lhs = asBool(mem.reg(d.b));
                bool rhs = asBool(mem.reg(d.c));
                bool res = (op == bc::Opcode::AND) ? (lhs && rhs) : (lhs || rhs);
                mem.reg(d.a) = Value::boolean(res);
                pc++;
                break;
            }

            // -------- jumps (pc = (pc+1) + sBx) --------
            case bc::Opcode::JMP: {
                auto d = bc::decodeAsBx(w);
                pc = (pc + 1) + d.sbx;
                break;
            }

            case bc::Opcode::JMP_T: {
                auto d = bc::decodeAsBx(w);
                bool cond = asBool(mem.reg(d.a));
                pc = cond ? ((pc + 1) + d.sbx) : (pc + 1);
                break;
            }

            case bc::Opcode::JMP_F: {
                auto d = bc::decodeAsBx(w);
                bool cond = asBool(mem.reg(d.a));
                pc = (!cond) ? ((pc + 1) + d.sbx) : (pc + 1);
                break;
            }

            // -------- cast --------
            case bc::Opcode::I2F: {
                auto d = bc::decodeABC(w);
                int32_t v = asInt(mem.reg(d.b));
                mem.reg(d.a) = Value::f32(static_cast<float>(v));
                pc++;
                break;
            }

            // -------- return --------
            case bc::Opcode::RET: {
                auto d = bc::decodeABC(w);
                Value ret = Value::nil();
                if (d.a != 255) ret = mem.reg(d.a);

                mem.popFrame();
                return ret;
            }

            default:
                mem.popFrame();
                throw std::runtime_error("Unknown opcode");
        }

        // Место для будущего GC-триггера:
        // if (needGC) { mem.markRoots(); mem.heap.sweep(); }
    }

    mem.popFrame();
    throw std::runtime_error("Bytecode error: no RET reached");
}

} // namespace alkv::vm