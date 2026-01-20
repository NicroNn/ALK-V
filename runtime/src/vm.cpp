#include "alkv/vm.hpp"
#include <cmath>
#include <stdexcept>
#include <iostream>
#include <chrono>
#include "alkv/vm/object.hpp"

namespace alkv::vm {

// ======= ВСПОМОГАТЕЛЬНЫЕ ФУНКЦИИ =======
static ObjArray* asArray(const Value& v) {
    if (v.tag != ValueTag::Obj || !v.as.obj || v.as.obj->type != ObjType::Array)
        throw std::runtime_error("TypeError: expected array");
    return static_cast<ObjArray*>(v.as.obj);
}

static ObjInstance* asInstance(const Value& v) {
    if (v.tag != ValueTag::Obj || !v.as.obj || v.as.obj->type != ObjType::Instance)
        throw std::runtime_error("TypeError: expected object instance");
    return static_cast<ObjInstance*>(v.as.obj);
}

static ObjFuncRef* asFuncRef(const Value& v) {
    if (v.tag != ValueTag::Obj || !v.as.obj || v.as.obj->type != ObjType::FuncRef)
        throw std::runtime_error("TypeError: expected func ref");
    return static_cast<ObjFuncRef*>(v.as.obj);
}

static ObjClassRef* asClassRef(const Value& v) {
    if (v.tag != ValueTag::Obj || !v.as.obj || v.as.obj->type != ObjType::ClassRef)
        throw std::runtime_error("TypeError: expected class ref");
    return static_cast<ObjClassRef*>(v.as.obj);
}

static ObjFieldRef* asFieldRef(const Value& v) {
    if (v.tag != ValueTag::Obj || !v.as.obj || v.as.obj->type != ObjType::FieldRef)
        throw std::runtime_error("TypeError: expected field ref");
    return static_cast<ObjFieldRef*>(v.as.obj);
}

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

void VM::loadModule(const std::vector<bc::LoadedFunction>& fns) {
    fnByName_.clear();
    fnByName_.reserve(fns.size());
    for (const auto& f : fns) {
        fnByName_[f.name] = &f;
    }
}

// ---- минимальная VM-layout таблица полей: className -> fieldName -> slotIndex
static std::unordered_map<std::string, std::unordered_map<std::string, std::size_t>> g_fieldSlots;

static std::size_t getFieldSlot(std::string_view cls, std::string_view field) {
    auto& fm = g_fieldSlots[std::string(cls)];
    auto it = fm.find(std::string(field));
    if (it != fm.end()) return it->second;
    std::size_t slot = fm.size();
    fm[std::string(field)] = slot;
    return slot;
}

// ---- native calls ----
static Value callNative(VMMemory& mem, uint32_t nativeId, uint32_t argc) {
    auto getArg = [&](uint32_t i) -> const Value& {
        if (i >= argc) throw std::runtime_error("CALL_NATIVE: arg OOB");
        return mem.reg(static_cast<uint16_t>(i));
    };

    switch (nativeId) {
        case 1: { // ochev.Out(x)
            if (argc != 1) throw std::runtime_error("ochev.Out expects 1 arg");
            const Value& v = getArg(0);
            if (v.tag == ValueTag::Int) std::cout << v.as.i;
            else if (v.tag == ValueTag::Float) std::cout << v.as.f;
            else if (v.tag == ValueTag::Bool) std::cout << (v.as.b ? "true" : "false");
            else if (v.tag == ValueTag::Nil) std::cout << "nil";
            else if (v.tag == ValueTag::Obj && v.as.obj && v.as.obj->type == ObjType::String)
                std::cout << static_cast<ObjString*>(v.as.obj)->view();
            else
                std::cout << "<obj>";
            std::cout << "\n";
            return Value::nil();
        }
        case 2: { // ochev.In() -> string
            if (argc != 0) throw std::runtime_error("ochev.In expects 0 args");
            std::string line;
            std::getline(std::cin, line);
            auto* s = mem.heap.allocString(line);
            return Value::object(s);
        }
        default:
            throw std::runtime_error("Unknown nativeId: " + std::to_string(nativeId));
    }
}

// Структура для отслеживания состояния VM
struct VMState {
    uint64_t instructionsExecuted = 0;
    uint64_t lastGcAtInstructions = 0;
    static constexpr uint64_t GC_INSTRUCTION_INTERVAL = 10000;
    
    bool shouldRunGC() {
        return (instructionsExecuted - lastGcAtInstructions) >= GC_INSTRUCTION_INTERVAL;
    }
    
    void recordGC() {
        lastGcAtInstructions = instructionsExecuted;
    }
};

Value VM::run(const std::string& entryName, const std::vector<Value>& args) {
    auto it = fnByName_.find(entryName);
    if (it == fnByName_.end()) throw std::runtime_error("Entry function not found: " + entryName);
    const bc::LoadedFunction* entry = it->second;

    // Инициализируем состояние VM
    VMState vmState;
    
    // push entry frame
    mem.pushFrame(&entry->fn, entry->fn.regCount, /*returnPc*/-1, /*returnDst*/255);

    // args -> R0.. convention
    if (args.size() > entry->fn.regCount) {
        mem.popFrame();
        throw std::runtime_error("Too many args for regCount");
    }
    for (size_t i = 0; i < args.size(); ++i) mem.reg(static_cast<uint16_t>(i)) = args[i];

    // интерпретатор
    while (!mem.callStack.empty()) {
        Frame& fr = mem.currentFrame();
        const auto& code = fr.fn->code;

        if (fr.pc < 0 || fr.pc >= static_cast<int32_t>(code.size())) {
            mem.popFrame();
            throw std::runtime_error("Bytecode error: pc out of bounds");
        }

        uint32_t w = code[fr.pc];
        bc::Opcode op = bc::decodeOp(w);
        
        // Увеличиваем счетчик инструкций
        vmState.instructionsExecuted++;
        
        // Периодический запуск GC (не только по памяти, но и по времени/инструкциям)
        if (vmState.shouldRunGC()) {
            mem.collectGarbage();
            vmState.recordGC();
        }

        switch (op) {
            case bc::Opcode::NOP: {
                fr.pc++;
                break;
            }
            case bc::Opcode::MOV: {
                auto d = abc(w);
                mem.reg(d.a) = mem.reg(d.b);
                fr.pc++;
                break;
            }
            case bc::Opcode::LOADK: {
                auto d = abx(w);
                if (d.bx >= fr.fn->constPool.size()) throw std::runtime_error("LOADK: const OOB");
                mem.reg(d.a) = fr.fn->constPool[d.bx];
                fr.pc++;
                break;
            }

            // ---- arithmetic int
            case bc::Opcode::ADD_I:
            case bc::Opcode::SUB_I:
            case bc::Opcode::MUL_I:
            case bc::Opcode::DIV_I:
            case bc::Opcode::MOD_I: {
                auto d = abc(w);
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
                fr.pc++;
                break;
            }

            // ---- arithmetic float
            case bc::Opcode::ADD_F:
            case bc::Opcode::SUB_F:
            case bc::Opcode::MUL_F:
            case bc::Opcode::DIV_F:
            case bc::Opcode::MOD_F: {
                auto d = abc(w);
                float lhs = asFloat(mem.reg(d.b));
                float rhs = asFloat(mem.reg(d.c));
                float res = 0;
                switch (op) {
                    case bc::Opcode::ADD_F: res = lhs + rhs; break;
                    case bc::Opcode::SUB_F: res = lhs - rhs; break;
                    case bc::Opcode::MUL_F: res = lhs * rhs; break;
                    case bc::Opcode::DIV_F: res = lhs / rhs; break;
                    case bc::Opcode::MOD_F: res = std::fmod(lhs, rhs); break;
                    default: break;
                }
                mem.reg(d.a) = Value::f32(res);
                fr.pc++;
                break;
            }

            // ---- comparisons -> bool
            case bc::Opcode::LT_I: case bc::Opcode::LE_I:
            case bc::Opcode::GT_I: case bc::Opcode::GE_I: {
                auto d = abc(w);
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
                fr.pc++;
                break;
            }
            case bc::Opcode::LT_F: case bc::Opcode::LE_F:
            case bc::Opcode::GT_F: case bc::Opcode::GE_F: {
                auto d = abc(w);
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
                fr.pc++;
                break;
            }

            case bc::Opcode::EQ:
            case bc::Opcode::NE: {
                auto d = abc(w);
                bool eq = valueEquals(mem.reg(d.b), mem.reg(d.c));
                mem.reg(d.a) = Value::boolean(op == bc::Opcode::EQ ? eq : !eq);
                fr.pc++;
                break;
            }

            case bc::Opcode::NOT: {
                auto d = abc(w);
                bool v = asBool(mem.reg(d.b));
                mem.reg(d.a) = Value::boolean(!v);
                fr.pc++;
                break;
            }

            // ---- jumps: pc = (pc+1) + sBx
            case bc::Opcode::JMP: {
                auto d = asbx(w);
                fr.pc = (fr.pc + 1) + d.sbx;
                break;
            }
            case bc::Opcode::JMP_T: {
                auto d = asbx(w);
                bool cond = asBool(mem.reg(d.a));
                fr.pc = cond ? ((fr.pc + 1) + d.sbx) : (fr.pc + 1);
                break;
            }
            case bc::Opcode::JMP_F: {
                auto d = asbx(w);
                bool cond = asBool(mem.reg(d.a));
                fr.pc = (!cond) ? ((fr.pc + 1) + d.sbx) : (fr.pc + 1);
                break;
            }

            case bc::Opcode::I2F: {
                auto d = abc(w);
                int32_t v = asInt(mem.reg(d.b));
                mem.reg(d.a) = Value::f32(static_cast<float>(v));
                fr.pc++;
                break;
            }

            // ---- arrays
            case bc::Opcode::NEW_ARR: {
                auto d = abc(w);
                int32_t n = asInt(mem.reg(d.b));
                if (n < 0) throw std::runtime_error("NEW_ARR: negative size");
                auto* arr = mem.heap.allocArray(static_cast<std::size_t>(n));
                mem.reg(d.a) = Value::object(arr);
                fr.pc++;
                break;
            }
            case bc::Opcode::GET_ELEM: {
                auto d = abc(w);
                auto* arr = asArray(mem.reg(d.b));
                int32_t idx = asInt(mem.reg(d.c));
                if (idx < 0 || idx >= static_cast<int32_t>(arr->elems.size()))
                    throw std::runtime_error("GET_ELEM: index OOB");
                mem.reg(d.a) = arr->elems[static_cast<std::size_t>(idx)];
                fr.pc++;
                break;
            }
            case bc::Opcode::SET_ELEM: {
                auto d = abc(w);
                auto* arr = asArray(mem.reg(d.a));
                int32_t idx = asInt(mem.reg(d.b));
                if (idx < 0 || idx >= static_cast<int32_t>(arr->elems.size()))
                    throw std::runtime_error("SET_ELEM: index OOB");
                arr->elems[static_cast<std::size_t>(idx)] = mem.reg(d.c);
                fr.pc++;
                break;
            }

            // ---- objects
            case bc::Opcode::NEW_OBJ: {
                auto d = abx(w);
                if (d.bx >= fr.fn->constPool.size()) throw std::runtime_error("NEW_OBJ: const OOB");
                auto* cr = asClassRef(fr.fn->constPool[d.bx]);
                auto* inst = mem.heap.allocInstance(cr->name);
                mem.reg(d.a) = Value::object(inst);
                fr.pc++;
                break;
            }

            // GET_FIELD/SET_FIELD: ABC с fieldRefReg
            case bc::Opcode::GET_FIELD: {
                auto d = abc(w);
                auto* inst = asInstance(mem.reg(d.b));
                auto* fld = asFieldRef(mem.reg(d.c));

                std::size_t slot = getFieldSlot(fld->className->view(), fld->fieldName->view());
                if (inst->fields.size() <= slot) inst->fields.resize(slot + 1, Value::nil());

                mem.reg(d.a) = inst->fields[slot];
                fr.pc++;
                break;
            }

            case bc::Opcode::SET_FIELD: {
                auto d = abc(w);
                auto* inst = asInstance(mem.reg(d.a));
                auto* fld = asFieldRef(mem.reg(d.b));
                std::size_t slot = getFieldSlot(fld->className->view(), fld->fieldName->view());
                if (inst->fields.size() <= slot) inst->fields.resize(slot + 1, Value::nil());

                inst->fields[slot] = mem.reg(d.c);
                fr.pc++;
                break;
            }

            // ---- calls
            case bc::Opcode::CALL: {
                auto d = abc(w);
                auto* fref = asFuncRef(mem.reg(d.b));
                auto itf = fnByName_.find(std::string(fref->name->view()));
                if (itf == fnByName_.end()) throw std::runtime_error("CALL: unknown function");

                const bc::LoadedFunction* callee = itf->second;

                int32_t returnPc = fr.pc + 1;
                uint8_t returnDst = d.a;

                mem.pushFrame(&callee->fn, callee->fn.regCount, returnPc, returnDst);
                break;
            }

            case bc::Opcode::CALLK: {
                auto d = abx(w);
                if (d.bx >= fr.fn->constPool.size()) throw std::runtime_error("CALLK: const OOB");

                auto* fref = asFuncRef(fr.fn->constPool[d.bx]);
                auto itf = fnByName_.find(std::string(fref->name->view()));
                if (itf == fnByName_.end()) {
                    throw std::runtime_error("CALLK: function not found: " + std::string(fref->name->view()));
                }

                const bc::LoadedFunction* callee = itf->second;

                int32_t returnPc = fr.pc + 1;
                uint8_t returnDst = d.a;

                mem.pushFrame(&callee->fn, callee->fn.regCount, returnPc, returnDst);
                break;
            }

            case bc::Opcode::CALL_NATIVE: {
                auto d = abc(w);
                uint32_t nativeId = d.b;
                uint32_t argc = d.c;
                Value ret = callNative(mem, nativeId, argc);
                mem.reg(d.a) = ret;
                fr.pc++;
                break;
            }

            case bc::Opcode::RET: {
                auto d = abc(w);
                Value ret = Value::nil();
                if (d.a != 255) ret = mem.reg(d.a);

                int32_t returnPc = fr.returnPc;
                uint8_t returnDst = fr.returnDst;

                mem.popFrame();

                if (mem.callStack.empty()) {
                    mem.updateStats();
                    return ret;
                }

                // if (mem.callStack.empty()) {
                //     // Выводим статистику GC перед возвратом
                //     mem.updateStats();
                //     std::cout << "\n=== VM Statistics ===\n";
                //     mem.stats.print();
                //     std::cout << "Instructions executed: " << vmState.instructionsExecuted << "\n";
                //     std::cout << "=====================\n";
                //     return ret;
                // }

                Frame& caller = mem.currentFrame();
                caller.pc = returnPc;
                if (returnDst != 255) mem.reg(returnDst) = ret;
                break;
            }

            default:
                throw std::runtime_error("Unknown opcode");
        }
    }

    return Value::nil();
}

} // namespace alkv::vm