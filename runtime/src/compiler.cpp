#include "alkv/compiler/compiler.hpp"
#include <cstdio>

using namespace alkv;
bool valueEquals(const vm::Value* a, const vm::Value* b) {
    if (a->tag != b->tag) return false;
    switch (a->tag) {
        case vm::ValueTag::Nil:   return true;
        case vm::ValueTag::Int:   return a->as.i == b->as.i;
        case vm::ValueTag::Float: return a->as.f == b->as.f;
        case vm::ValueTag::Bool:  return a->as.b == b->as.b;
        case vm::ValueTag::Obj: {
            if (a->as.obj == b->as.obj) return true;
            if (!a->as.obj || !b->as.obj) return false;
            if (a->as.obj->type != b->as.obj->type) return false;
            if (a->as.obj->type == vm::ObjType::String) {
                auto* sa = static_cast<vm::ObjString*>(a->as.obj);
                auto* sb = static_cast<vm::ObjString*>(b->as.obj);
                return sa->view() == sb->view();
            }
            return false;
        }
    }
    return false;
}

vm::ObjArray* allocArrayWrapper(vm::VMMemory* mem, std::size_t n) {
    return mem->heap.allocArray(n);
}

vm::ObjInstance* allocInstanceWrapper(vm::VMMemory* mem, vm::ObjString* name) {
    return mem->heap.allocInstance(name);
}

uintptr_t getFieldSlotAddress(g_fs_T* g_fs,
                              vm::ObjInstance* inst, vm::ObjFieldRef* fld) {
    auto& fm = (*g_fs)[std::string(fld->className->view())];
    auto it = fm.find(std::string(fld->fieldName->view()));
    std::size_t slot;
    if (it != fm.end()) {
        slot = it -> second;
    } else {
        slot = fm.size();
        fm[std::string(fld->fieldName->view())] = slot;
    }
    if (inst->fields.size() <= slot) inst->fields.resize(slot + 1, vm::Value::nil());
    return reinterpret_cast<uintptr_t>(&inst->fields[slot]);
}

void call_function(fnByN_T* fnByN, vm::VMMemory* mem,
                   uint8_t returnDst, vm::ObjFuncRef* fref, uint32_t argc) {
    auto itf = (*fnByN).find(std::string(fref->name->view()));
    if (itf == (*fnByN).end()) throw std::runtime_error("CALL: unknown function");
    const bc::LoadedFunction* callee = itf->second;

    // сохранить аргументы из caller R0..R(argc-1)
    std::vector<vm::Value> argv;
    argv.reserve(argc);
    for (uint32_t i = 0; i < argc; ++i) argv.push_back(mem->reg(static_cast<uint16_t>(i)));

    int32_t returnPc = mem->currentFrame().pc + 1;

    mem->pushFrame(&callee->fn, callee->fn.regCount, returnPc, returnDst);

    if (argc > callee->fn.regCount) {
        throw std::runtime_error("CALL: too many args for callee regCount");
    }
    for (uint32_t i = 0; i < argc; ++i) mem->reg(static_cast<uint16_t>(i)) = argv[i];
}

void return_from_function(vm::VMMemory* mem, vm::Value* entry_ret_ptr, uint8_t* end_flag, uint8_t r) {
    vm::Frame* fr = &(mem->currentFrame());
    vm::Value ret = vm::Value::nil();
    if (r != 255) ret = mem->reg(r);

    int32_t returnPc = fr -> returnPc;
    uint8_t returnDst = fr -> returnDst;

    mem->popFrame();

    if (mem->callStack.empty()) { // ?????
        // возврат из entry
        mem->updateStats();
        *entry_ret_ptr = ret;
        *end_flag = 1;
        return;
    }

    // restore caller pc and place return
    vm::Frame& caller = mem->currentFrame();
    caller.pc = returnPc;
    if (returnDst != 255) mem->reg(returnDst) = ret;
}

void callNative(vm::VMMemory* mem, uint32_t nativeId, uint32_t argc, uint16_t a) {
    using namespace vm;
    auto getArg = [&](uint32_t i) -> const Value& {
        if (i >= argc) throw std::runtime_error("CALL_NATIVE: arg OOB");
        return mem->reg(static_cast<uint16_t>(i));
    };
    auto asIntLocal = [&](const Value& v) -> int32_t {
        if (v.tag != ValueTag::Int) throw std::runtime_error("TypeError: expected int");
        return v.as.i;
    };
    auto asFloatLocal = [&](const Value& v) -> float {
        if (v.tag != ValueTag::Float) throw std::runtime_error("TypeError: expected float");
        return v.as.f;
    };

    auto numericMax = [&](const Value& a, const Value& b) -> Value {
        if (a.tag == ValueTag::Int && b.tag == ValueTag::Int) {
            return Value::i32(std::max(a.as.i, b.as.i));
        }
        if (a.tag == ValueTag::Float && b.tag == ValueTag::Float) {
            return Value::f32(std::max(a.as.f, b.as.f));
        }
        if (a.tag == ValueTag::Int && b.tag == ValueTag::Float) {
            return Value::f32(std::max(static_cast<float>(a.as.i), b.as.f));
        }
        if (a.tag == ValueTag::Float && b.tag == ValueTag::Int) {
            return Value::f32(std::max(a.as.f, static_cast<float>(b.as.i)));
        }
        throw std::runtime_error("TypeError: max expects (int|float, int|float)");
    };
    auto numericMin = [&](const Value& a, const Value& b) -> Value {
        if (a.tag == ValueTag::Int && b.tag == ValueTag::Int) {
            return Value::i32(std::min(a.as.i, b.as.i));
        }
        if (a.tag == ValueTag::Float && b.tag == ValueTag::Float) {
            return Value::f32(std::min(a.as.f, b.as.f));
        }
        if (a.tag == ValueTag::Int && b.tag == ValueTag::Float) {
            return Value::f32(std::min(static_cast<float>(a.as.i), b.as.f));
        }
        if (a.tag == ValueTag::Float && b.tag == ValueTag::Int) {
            return Value::f32(std::min(a.as.f, static_cast<float>(b.as.i)));
        }
        throw std::runtime_error("TypeError: min expects (int|float, int|float)");
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
            mem->reg(a) = Value::nil();
            return;
        }

        case 2: { // ochev.In() -> string
            if (argc != 0) throw std::runtime_error("ochev.In expects 0 args");
            std::string line;
            std::getline(std::cin, line);
            auto* s = mem->heap.allocString(line);
            mem->reg(a) = Value::object(s);
            return;
        }

        case 3: { // ochev.TudaSyuda(...)
            // Вариант A: (arr, i, j)
            if (argc == 3) {
                ObjArray* arr = static_cast<ObjArray*>(getArg(0).as.obj);
                int32_t i = asIntLocal(getArg(1));
                int32_t j = asIntLocal(getArg(2));
                if (i < 0 || j < 0 ||
                    i >= static_cast<int32_t>(arr->elems.size()) ||
                    j >= static_cast<int32_t>(arr->elems.size())) {
                    throw std::runtime_error("ochev.TudaSyuda: index OOB");
                }
                Value* e1 = &arr->elems[static_cast<std::size_t>(i)];
                Value* e2 = &arr->elems[static_cast<std::size_t>(j)];
                auto* temp = new Value();
                *temp = *e2;
                *e2 = *e1;
                *e1 = *temp;
                delete temp;
                mem->reg(a) = Value::nil();
                return;
            }
            // Вариант B: (arr1, i1, arr2, i2)
            if (argc == 4) {
                ObjArray* a1 = static_cast<ObjArray*>(getArg(0).as.obj);
                int32_t i1 = asIntLocal(getArg(1));
                ObjArray* a2 = static_cast<ObjArray*>(getArg(2).as.obj);
                int32_t i2 = asIntLocal(getArg(3));
                if (i1 < 0 || i1 >= static_cast<int32_t>(a1->elems.size()) ||
                    i2 < 0 || i2 >= static_cast<int32_t>(a2->elems.size())) {
                    throw std::runtime_error("ochev.TudaSyuda: index OOB");
                }
                Value* e1 = &a1->elems[static_cast<std::size_t>(i1)];
                Value* e2 = &a2->elems[static_cast<std::size_t>(i2)];
                auto* temp = new Value();
                *temp = *e2;
                *e2 = *e1;
                *e1 = *temp;
                delete temp;
                mem->reg(a) = Value::nil();
                return;
            }
            throw std::runtime_error("ochev.TudaSyuda expects 3 args (arr,i,j) or 4 args (arr1,i1,arr2,i2)");
        }

        case 4: { // ochev.>>>(a, b) -> max
            if (argc != 2) throw std::runtime_error("ochev.>>> expects 2 args");
            mem->reg(a) = numericMax(getArg(0), getArg(1));
            return;
        }

        case 5: { // ochev.<<<(a, b) -> min
            if (argc != 2) throw std::runtime_error("ochev.<<< expects 2 args");
            mem->reg(a) = numericMin(getArg(0), getArg(1));
            return;
        }

        default:
            throw std::runtime_error("Unknown nativeId: " + std::to_string(nativeId));
    }
}

namespace alkv::compiler {
    Compiler::Compiler(vm::VMMemory& m,
                       g_fs_T& g_fs,
                       fnByN_T& fnByN,
                       bool logging, bool error_handling) : mem(m), g_fieldSlots(g_fs), fnByName_(fnByN) {
        is_logging = logging;
        is_error_handling = error_handling;
    }

    Func Compiler::create_func(vm::Value*& entry_ret_ptr, uint8_t* end_flag, uint32_t size) {
        asmjit::CodeHolder code;
        asmjit::FileLogger logger(stdout);
        MyErrorHandler handler;
        code.init(rt.environment(), rt.cpu_features());
        if (is_logging) {
            code.set_logger(&logger);
        }
        if (is_error_handling) {
            code.set_error_handler(&handler);
        }
        asmjit::x86::Assembler a(&code);

        std::map<int, asmjit::Label> jmp_labels;
        for (uint32_t i = 0; i < size; ++i) {
            vm::Frame& fr = mem.currentFrame();
            uint32_t dw = fr.fn->code[fr.pc + i];
            bc::Opcode op = bc::decodeOp(dw);
            if (op == bc::Opcode::JMP || op == bc::Opcode::JMP_F || op == bc::Opcode::JMP_T) {
                auto d = bc::decodeAsBx(dw);
                int32_t dst = i + d.sbx + 1;
                if (dst >= 0 || static_cast<uint32_t>(dst) < size) {
                    jmp_labels[dst] = a.new_label();
                }
            }
        }

        asmjit::Label instant_ret = a.new_label();
        std::unordered_map<uint16_t, vm::Value> const_regs;
        const bool const_folding = false;
        if (is_logging) {
            printf("this frame's consts:\n");
            int _ = 0;
            for (vm::Value v : mem.currentFrame().fn->constPool) {
                switch (v.tag) {
                    case vm::ValueTag::Nil: printf("c%d = NULL\n", _); break;
                    case vm::ValueTag::Int: printf("c%d = %di\n", _, v.as.i); break;
                    case vm::ValueTag::Float: printf("c%d = %ff\n", _, v.as.f); break;
                    case vm::ValueTag::Bool: printf("c%d = %db\n", _, v.as.b); break;
                    case vm::ValueTag::Obj: printf("c%d = obj<%llu>\n", _, (uintptr_t)v.as.obj); break;
                }
                ++_;
            }
            printf("\n");
            printf("this frame's code:\n");
        }

        for (uint32_t i = 0; i < size; ++i) {
            vm::Frame& fr = mem.currentFrame();
            if (jmp_labels.contains(i)) {
                a.bind(jmp_labels[i]);
            }
            uint32_t dw = fr.fn->code[fr.pc + i];
            bc::Opcode op = bc::decodeOp(dw);
            switch (op) {
                case bc::Opcode::NOP: {
                    if (is_logging) {
                        printf("NOP\n");
                    }
                    break;
                }

                case bc::Opcode::MOV: {
                    auto d = bc::decodeABC(dw);
                    if (is_logging) {
                        printf("MOVE r%d <- r%d\n", d.a, d.b);
                    }
                    if (const_folding && const_regs.contains(d.b)) {
                        asmjit::x86::Mem dst_v_ptr = asmjit::x86::ptr_64(reinterpret_cast<uintptr_t>(&mem.reg(d.a).as.obj));
                        asmjit::x86::Mem dst_t_ptr = asmjit::x86::ptr_64(reinterpret_cast<uintptr_t>(&mem.reg(d.a).tag));
                        a.mov(dst_v_ptr, const_regs[d.b].as.obj);
                        a.mov(dst_t_ptr, const_regs[d.b].tag);
                        const_regs[d.a] = const_regs[d.b];
                    } else {
                        const_regs.erase(d.a);
                        asmjit::x86::Mem dst_v_ptr = asmjit::x86::ptr_64(reinterpret_cast<uintptr_t>(&mem.reg(d.a).as.obj));
                        asmjit::x86::Mem src_v_ptr = asmjit::x86::ptr_64(reinterpret_cast<uintptr_t>(&mem.reg(d.b).as.obj));
                        asmjit::x86::Mem dst_t_ptr = asmjit::x86::ptr_64(reinterpret_cast<uintptr_t>(&mem.reg(d.a).tag));
                        asmjit::x86::Mem src_t_ptr = asmjit::x86::ptr_64(reinterpret_cast<uintptr_t>(&mem.reg(d.b).tag));
                        a.mov(asmjit::x86::rbx, src_v_ptr);
                        a.mov(dst_v_ptr, asmjit::x86::rbx);
                        a.mov(asmjit::x86::rbx, src_t_ptr);
                        a.mov(dst_t_ptr, asmjit::x86::rbx);
                    }
                    break;
                }

                case bc::Opcode::LOADK: {
                    auto d = bc::decodeABx(dw);
                    if (is_logging) {
                        printf("LOAD_CONST r%d <- c%d\n", d.a, d.bx);
                    }
                    asmjit::x86::Mem dst_v_ptr = asmjit::x86::ptr_64(reinterpret_cast<uintptr_t>(&mem.reg(d.a).as.obj));
                    asmjit::x86::Mem dst_t_ptr = asmjit::x86::ptr_64(reinterpret_cast<uintptr_t>(&mem.reg(d.a).tag));
                    asmjit::Imm c = fr.fn->constPool[d.bx].as.obj;
                    asmjit::Imm t = fr.fn->constPool[d.bx].tag;
                    a.mov(dst_v_ptr, c);
                    a.mov(dst_t_ptr, t);
                    const_regs[d.a] = fr.fn->constPool[d.bx];
                    break;
                }

                case bc::Opcode::ADD_I:
                case bc::Opcode::SUB_I:
                case bc::Opcode::MUL_I:
                case bc::Opcode::DIV_I:
                case bc::Opcode::MOD_I: {
                    auto d = bc::decodeABC(dw);
                    if (is_logging) {
                        switch (op) {
                            case bc::Opcode::ADD_I: printf("ADD_INT r%d <- r%d + r%d\n", d.a, d.b, d.c); break;
                            case bc::Opcode::SUB_I: printf("SUB_INT r%d <- r%d - r%d\n", d.a, d.b, d.c); break;
                            case bc::Opcode::MUL_I: printf("MUL_INT r%d <- r%d * r%d\n", d.a, d.b, d.c); break;
                            case bc::Opcode::DIV_I: printf("DIV_INT r%d <- r%d / r%d\n", d.a, d.b, d.c); break;
                            case bc::Opcode::MOD_I: printf("MOD_INT r%d <- r%d %% r%d\n", d.a, d.b, d.c); break;
                            default: break;
                        }
                    }
                    if (const_folding && const_regs.contains(d.b) && const_regs.contains(d.c)) {
                        int32_t src1 = const_regs[d.b].as.i;
                        int32_t src2 = const_regs[d.c].as.i;
                        asmjit::x86::Mem dst_v_ptr = asmjit::x86::ptr_32(reinterpret_cast<uintptr_t>(&mem.reg(d.a).as.i));
                        asmjit::x86::Mem dst_t_ptr = asmjit::x86::ptr_32(reinterpret_cast<uintptr_t>(&mem.reg(d.a).tag));
                        int32_t res;
                        switch (op) {
                            case bc::Opcode::ADD_I: res = src1 + src2; break;
                            case bc::Opcode::SUB_I: res = src1 - src2; break;
                            case bc::Opcode::MUL_I: res = src1 * src2; break;
                            case bc::Opcode::DIV_I: res = src1 / src2; break;
                            case bc::Opcode::MOD_I: res = src1 % src2; break;
                            default: break;
                        }
                        a.mov(dst_v_ptr, res);
                        a.mov(dst_t_ptr, 1);
                        const_regs[d.a] = vm::Value::i32(res);
                    } else {
                        const_regs.erase(d.a);
                        asmjit::x86::Mem src1_ptr = asmjit::x86::ptr_32(reinterpret_cast<uintptr_t>(&mem.reg(d.b).as.i));
                        asmjit::x86::Mem src2_ptr = asmjit::x86::ptr_32(reinterpret_cast<uintptr_t>(&mem.reg(d.c).as.i));
                        asmjit::x86::Mem dst_v_ptr = asmjit::x86::ptr_32(reinterpret_cast<uintptr_t>(&mem.reg(d.a).as.i));
                        asmjit::x86::Mem dst_t_ptr = asmjit::x86::ptr_32(reinterpret_cast<uintptr_t>(&mem.reg(d.a).tag));
                        asmjit::x86::Gp temp = asmjit::x86::eax;
                        a.mov(asmjit::x86::eax, src1_ptr);
                        switch (op) {
                            case bc::Opcode::ADD_I: a.add(asmjit::x86::eax, src2_ptr); break;
                            case bc::Opcode::SUB_I: a.sub(asmjit::x86::eax, src2_ptr); break;
                            case bc::Opcode::MUL_I: a.imul(asmjit::x86::eax, src2_ptr); break;
                            case bc::Opcode::DIV_I:
                            case bc::Opcode::MOD_I: {
                                a.xor_(asmjit::x86::rdx, asmjit::x86::rdx);
                                a.idiv(src2_ptr);
                                switch (op) {
                                    case bc::Opcode::MOD_I: temp = asmjit::x86::edx; break;
                                    default: break;
                                }
                            }
                            default: break;
                        }
                        a.mov(dst_v_ptr, temp);
                        a.mov(dst_t_ptr, 1);
                    }
                    break;
                }

                case bc::Opcode::ADD_F:
                case bc::Opcode::SUB_F:
                case bc::Opcode::MUL_F:
                case bc::Opcode::DIV_F:
                case bc::Opcode::MOD_F: {
                    auto d = bc::decodeABC(dw);
                    if (is_logging) {
                        switch (op) {
                            case bc::Opcode::ADD_F: printf("ADD_FLOAT r%d <- r%d + r%d\n", d.a, d.b, d.c); break;
                            case bc::Opcode::SUB_F: printf("SUB_FLOAT r%d <- r%d - r%d\n", d.a, d.b, d.c); break;
                            case bc::Opcode::MUL_F: printf("MUL_FLOAT r%d <- r%d * r%d\n", d.a, d.b, d.c); break;
                            case bc::Opcode::DIV_F: printf("DIV_FLOAT r%d <- r%d / r%d\n", d.a, d.b, d.c); break;
                            case bc::Opcode::MOD_F: printf("MOD_FLOAT r%d <- r%d %% r%d\n", d.a, d.b, d.c); break;
                            default: break;
                        }
                    }
                    if (const_folding && const_regs.contains(d.b) && const_regs.contains(d.c)) {
                        float src1 = const_regs[d.b].as.f;
                        float src2 = const_regs[d.c].as.f;
                        asmjit::x86::Mem dst_v_ptr = asmjit::x86::ptr_32(reinterpret_cast<uintptr_t>(&mem.reg(d.a).as.f));
                        asmjit::x86::Mem dst_t_ptr = asmjit::x86::ptr_32(reinterpret_cast<uintptr_t>(&mem.reg(d.a).tag));
                        float res;
                        switch (op) {
                            case bc::Opcode::ADD_F: res = src1 + src2; break;
                            case bc::Opcode::SUB_F: res = src1 - src2; break;
                            case bc::Opcode::MUL_F: res = src1 * src2; break;
                            case bc::Opcode::DIV_F: res = src1 / src2; break;
                            case bc::Opcode::MOD_F: res = std::fmod(src1, src2); break;
                            default: break;
                        }
                        a.mov(dst_v_ptr, res);
                        a.mov(dst_t_ptr, 2);
                        const_regs[d.a] = vm::Value::f32(res);
                    } else {
                        const_regs.erase(d.a);
                        asmjit::x86::Mem src1_ptr = asmjit::x86::ptr_32(reinterpret_cast<uintptr_t>(&mem.reg(d.b).as.f));
                        asmjit::x86::Mem src2_ptr = asmjit::x86::ptr_32(reinterpret_cast<uintptr_t>(&mem.reg(d.c).as.f));
                        asmjit::x86::Mem dst_v_ptr = asmjit::x86::ptr_32(reinterpret_cast<uintptr_t>(&mem.reg(d.a).as.f));
                        asmjit::x86::Mem dst_t_ptr = asmjit::x86::ptr_32(reinterpret_cast<uintptr_t>(&mem.reg(d.a).tag));
                        a.movd(asmjit::x86::xmm0, src1_ptr);
                        switch (op) {
                            case bc::Opcode::ADD_F: a.addss(asmjit::x86::xmm0, src2_ptr); break;
                            case bc::Opcode::SUB_F: a.subss(asmjit::x86::xmm0, src2_ptr); break;
                            case bc::Opcode::MUL_F: a.mulss(asmjit::x86::xmm0, src2_ptr); break;
                            case bc::Opcode::DIV_F:
                            case bc::Opcode::MOD_F: {
                                a.divss(asmjit::x86::xmm0, src2_ptr);
                                switch (op) {
                                    case bc::Opcode::MOD_F:
                                        a.cvttss2si(asmjit::x86::eax, asmjit::x86::xmm0);
                                        a.movd(asmjit::x86::xmm0, src1_ptr);
                                        a.movd(asmjit::x86::xmm1, asmjit::x86::eax);
                                        a.mulss(asmjit::x86::xmm1, src2_ptr);
                                        a.subss(asmjit::x86::xmm0, asmjit::x86::xmm1); // highly inefficient probably
                                        break;
                                    default: break;
                                }
                            }
                            default: break;
                        }
                        a.movd(dst_v_ptr, asmjit::x86::xmm0);
                        a.mov(dst_t_ptr, 2);
                    }
                    break;
                }

                case bc::Opcode::LT_I: case bc::Opcode::LE_I:
                case bc::Opcode::GT_I: case bc::Opcode::GE_I: {
                    auto d = bc::decodeABC(dw);
                    if (is_logging) {
                        switch (op) {
                            case bc::Opcode::LT_I: printf("LT_INT r%d <- r%d < r%d\n", d.a, d.b, d.c); break;
                            case bc::Opcode::LE_I: printf("LE_INT r%d <- r%d <= r%d\n", d.a, d.b, d.c); break;
                            case bc::Opcode::GE_I: printf("GE_INT r%d <- r%d > r%d\n", d.a, d.b, d.c); break;
                            case bc::Opcode::GT_I: printf("GT_INT r%d <- r%d >= r%d\n", d.a, d.b, d.c); break;
                            default: break;
                        }
                    }
                    if (const_folding && const_regs.contains(d.b) && const_regs.contains(d.c)) {
                        int32_t src1 = const_regs[d.b].as.i;
                        int32_t src2 = const_regs[d.c].as.i;
                        asmjit::x86::Mem dst_v_ptr = asmjit::x86::ptr_8(reinterpret_cast<uintptr_t>(&mem.reg(d.a).as.b));
                        asmjit::x86::Mem dst_t_ptr = asmjit::x86::ptr_8(reinterpret_cast<uintptr_t>(&mem.reg(d.a).tag));
                        uint8_t res;
                        switch (op) {
                            case bc::Opcode::LT_I: res = src1 < src2;  break;
                            case bc::Opcode::LE_I: res = src1 <= src2; break;
                            case bc::Opcode::GT_I: res = src1 > src2;  break;
                            case bc::Opcode::GE_I: res = src1 >= src2; break;
                            default: break;
                        }
                        a.mov(dst_v_ptr, res);
                        a.mov(dst_t_ptr, 3);
                        const_regs[d.a] = vm::Value::boolean(res);
                    } else {
                        const_regs.erase(d.a);
                        asmjit::x86::Mem src1_ptr = asmjit::x86::ptr_32(reinterpret_cast<uintptr_t>(&mem.reg(d.b).as.i));
                        asmjit::x86::Mem src2_ptr = asmjit::x86::ptr_32(reinterpret_cast<uintptr_t>(&mem.reg(d.c).as.i));
                        asmjit::x86::Mem dst_v_ptr = asmjit::x86::ptr_8(reinterpret_cast<uintptr_t>(&mem.reg(d.a).as.b));
                        asmjit::x86::Mem dst_t_ptr = asmjit::x86::ptr_8(reinterpret_cast<uintptr_t>(&mem.reg(d.a).tag));
                        a.mov(asmjit::x86::eax, src1_ptr);
                        a.cmp(asmjit::x86::eax, src2_ptr);
                        asmjit::Label true_label = a.new_label();
                        asmjit::Label continue_label = a.new_label();
                        switch (op) {
                            case bc::Opcode::LT_I: a.jb(true_label);  break;
                            case bc::Opcode::LE_I: a.jbe(true_label); break;
                            case bc::Opcode::GT_I: a.ja(true_label);  break;
                            case bc::Opcode::GE_I: a.jae(true_label); break;
                            default: break;
                        }
                        a.mov(dst_v_ptr, 0);
                        a.jmp(continue_label);
                        a.bind(true_label);
                        a.mov(dst_v_ptr, 1);
                        a.bind(continue_label);
                        a.mov(dst_t_ptr, 3);
                    }
                    break;
                }

                case bc::Opcode::EQ:   case bc::Opcode::NE: { // smart comparison of objects
                    auto d = bc::decodeABC(dw);
                    if (is_logging) {
                        switch (op) {
                            case bc::Opcode::EQ: printf("EQ r%d <- r%d == r%d\n", d.a, d.b, d.c); break;
                            case bc::Opcode::NE: printf("NE r%d <- r%d != r%d\n", d.a, d.b, d.c); break;
                            default: break;
                        }
                    }
                    if (const_folding && const_regs.contains(d.b) && const_regs.contains(d.c)) {
                        asmjit::x86::Mem dst_v_ptr = asmjit::x86::ptr_8(reinterpret_cast<uintptr_t>(&mem.reg(d.a).as.b));
                        asmjit::x86::Mem dst_t_ptr = asmjit::x86::ptr_8(reinterpret_cast<uintptr_t>(&mem.reg(d.a).tag));
                        bool res = valueEquals(&const_regs[d.b], &const_regs[d.c]);
                        a.mov(dst_v_ptr, res);
                        a.mov(dst_t_ptr, 3);
                        const_regs[d.a] = vm::Value::boolean(res);
                    } else {
                        const_regs.erase(d.a);
                        asmjit::x86::Mem src1_ptr = asmjit::x86::ptr_32(reinterpret_cast<uintptr_t>(&mem.reg(d.b)));
                        asmjit::x86::Mem src2_ptr = asmjit::x86::ptr_32(reinterpret_cast<uintptr_t>(&mem.reg(d.c)));
                        asmjit::x86::Mem dst_v_ptr = asmjit::x86::ptr_8(reinterpret_cast<uintptr_t>(&mem.reg(d.a).as.b));
                        asmjit::x86::Mem dst_t_ptr = asmjit::x86::ptr_8(reinterpret_cast<uintptr_t>(&mem.reg(d.a).tag));
                        a.mov(asmjit::x86::eax, src1_ptr);
                        a.cmp(asmjit::x86::eax, src2_ptr);
                        asmjit::Label true_label = a.new_label();
                        asmjit::Label continue_label = a.new_label();
                        a.mov(asmjit::x86::rcx, src1_ptr);
                        a.mov(asmjit::x86::rdx, src2_ptr);
                        a.add(asmjit::x86::rsp, -128);
                        a.mov(asmjit::x86::r12, asmjit::x86::rsp);
                        a.and_(asmjit::x86::rsp, -16); // aligning before c++ function call
                        a.call(valueEquals);
                        a.mov(asmjit::x86::rsp, asmjit::x86::r12);
                        a.sub(asmjit::x86::rsp, -128);
                        a.cmp(asmjit::x86::rax, 1);
                        switch (op) {
                            case bc::Opcode::EQ:   a.je(true_label);  break;
                            case bc::Opcode::NE:   a.jne(true_label); break;
                            default: break;
                        }
                        a.mov(dst_v_ptr, 0);
                        a.jmp(continue_label);
                        a.bind(true_label);
                        a.mov(dst_v_ptr, 1);
                        a.bind(continue_label);
                        a.mov(dst_t_ptr, 3);
                        break;
                    }
                }

                case bc::Opcode::LT_F: case bc::Opcode::LE_F:
                case bc::Opcode::GT_F: case bc::Opcode::GE_F: {
                    auto d = bc::decodeABC(dw);
                    if (is_logging) {
                        switch (op) {
                            case bc::Opcode::LT_F: printf("LT_FLOAT r%d <- r%d < r%d\n", d.a, d.b, d.c); break;
                            case bc::Opcode::LE_F: printf("LE_FLOAT r%d <- r%d <= r%d\n", d.a, d.b, d.c); break;
                            case bc::Opcode::GE_F: printf("GE_FLOAT r%d <- r%d > r%d\n", d.a, d.b, d.c); break;
                            case bc::Opcode::GT_F: printf("GT_FLOAT r%d <- r%d >= r%d\n", d.a, d.b, d.c); break;
                            default: break;
                        }
                    }
                    if (const_folding && const_regs.contains(d.b) && const_regs.contains(d.c)) {
                        float src1 = const_regs[d.b].as.f;
                        float src2 = const_regs[d.c].as.f;
                        asmjit::x86::Mem dst_v_ptr = asmjit::x86::ptr_8(reinterpret_cast<uintptr_t>(&mem.reg(d.a).as.b));
                        asmjit::x86::Mem dst_t_ptr = asmjit::x86::ptr_8(reinterpret_cast<uintptr_t>(&mem.reg(d.a).tag));
                        uint8_t res;
                        switch (op) {
                            case bc::Opcode::LT_F: res = src1 < src2;  break;
                            case bc::Opcode::LE_F: res = src1 <= src2; break;
                            case bc::Opcode::GT_F: res = src1 > src2;  break;
                            case bc::Opcode::GE_F: res = src1 >= src2; break;
                            default: break;
                        }
                        a.mov(dst_v_ptr, res);
                        a.mov(dst_t_ptr, 3);
                        const_regs[d.a] = vm::Value::boolean(res);
                    } else {
                        const_regs.erase(d.a);
                        asmjit::x86::Mem src1_ptr = asmjit::x86::ptr_32(reinterpret_cast<uintptr_t>(&mem.reg(d.b).as.f));
                        asmjit::x86::Mem src2_ptr = asmjit::x86::ptr_32(reinterpret_cast<uintptr_t>(&mem.reg(d.c).as.f));
                        asmjit::x86::Mem dst_v_ptr = asmjit::x86::ptr_8(reinterpret_cast<uintptr_t>(&mem.reg(d.a).as.b));
                        asmjit::x86::Mem dst_t_ptr = asmjit::x86::ptr_8(reinterpret_cast<uintptr_t>(&mem.reg(d.a).tag));
                        a.movd(asmjit::x86::xmm0, src1_ptr);
                        asmjit::Label true_label = a.new_label();
                        asmjit::Label continue_label = a.new_label();
                        switch (op) {
                            case bc::Opcode::LT_F: a.cmpss(asmjit::x86::xmm0, src2_ptr, 1); break;
                            case bc::Opcode::LE_F: a.cmpss(asmjit::x86::xmm0, src2_ptr, 2); break;
                            case bc::Opcode::GT_F: a.cmpss(asmjit::x86::xmm0, src2_ptr, 6); break;
                            case bc::Opcode::GE_F: a.cmpss(asmjit::x86::xmm0, src2_ptr, 5); break;
                            default: break;
                        }
                        a.movd(asmjit::x86::eax, asmjit::x86::xmm0);
                        a.and_(asmjit::x86::eax, asmjit::x86::eax); // sets zero flag if eax is all 1
                        a.jnz(true_label);
                        a.mov(dst_v_ptr, 0);
                        a.jmp(continue_label);
                        a.bind(true_label);
                        a.mov(dst_v_ptr, 1);
                        a.bind(continue_label);
                        a.mov(dst_t_ptr, 3);
                    }
                    break;
                }

                case bc::Opcode::NOT: {
                    auto d = bc::decodeABC(dw);
                    if (is_logging) {
                        printf("NOT r%d <- ~r%d\n", d.a, d.b);
                    }
                    if (const_folding && const_regs.contains(d.b)) {
                        bool src = const_regs[d.b].as.b;
                        asmjit::x86::Mem dst_v_ptr = asmjit::x86::ptr_8(reinterpret_cast<uintptr_t>(&mem.reg(d.a).as.b));
                        asmjit::x86::Mem dst_t_ptr = asmjit::x86::ptr_8(reinterpret_cast<uintptr_t>(&mem.reg(d.a).tag));
                        a.mov(dst_t_ptr, !src);
                        a.mov(dst_t_ptr, 3);
                        const_regs[d.a] = vm::Value::boolean(!src);
                    } else {
                        const_regs.erase(d.a);
                        asmjit::x86::Mem src_ptr = asmjit::x86::ptr_8(reinterpret_cast<uintptr_t>(&mem.reg(d.b).as.b));
                        asmjit::x86::Mem dst_v_ptr = asmjit::x86::ptr_8(reinterpret_cast<uintptr_t>(&mem.reg(d.a).as.b));
                        asmjit::x86::Mem dst_t_ptr = asmjit::x86::ptr_8(reinterpret_cast<uintptr_t>(&mem.reg(d.a).tag));
                        asmjit::Label one_label = a.new_label();
                        asmjit::Label continue_label = a.new_label();
                        a.cmp(src_ptr, 0);
                        a.je(one_label);
                        a.mov(dst_v_ptr, 0);
                        a.jmp(continue_label);
                        a.bind(one_label);
                        a.mov(dst_v_ptr, 1);
                        a.bind(continue_label);
                        a.mov(dst_t_ptr, 3);
                    }
                    break;
                }

                /*
                case bc::Opcode::AND: {
                    auto d = bc::decodeABC(dw);
                    asmjit::x86::Mem src1_ptr = asmjit::x86::ptr_8(reinterpret_cast<uintptr_t>(&mem.reg(d.b).as.b));
                    asmjit::x86::Mem src2_ptr = asmjit::x86::ptr_8(reinterpret_cast<uintptr_t>(&mem.reg(d.c).as.b));
                    asmjit::x86::Mem dst_ptr = asmjit::x86::ptr_8(reinterpret_cast<uintptr_t>(&mem.reg(d.a).as.b));
                    asmjit::Label zero_label = a.new_label();
                    asmjit::Label continue_label = a.new_label();
                    a.cmp(src1_ptr, 0);
                    a.je(zero_label);
                    a.cmp(src2_ptr, 0);
                    a.je(zero_label);
                    a.mov(dst_ptr, 1);
                    a.jmp(continue_label);
                    a.bind(zero_label);
                    a.mov(dst_ptr, 0);
                    a.bind(continue_label);
                    break;
                }

                case bc::Opcode::OR: {
                    auto d = bc::decodeABC(dw);
                    asmjit::x86::Mem src1_ptr = asmjit::x86::ptr_8(reinterpret_cast<uintptr_t>(&mem.reg(d.b).as.b));
                    asmjit::x86::Mem src2_ptr = asmjit::x86::ptr_8(reinterpret_cast<uintptr_t>(&mem.reg(d.c).as.b));
                    asmjit::x86::Mem dst_ptr = asmjit::x86::ptr_8(reinterpret_cast<uintptr_t>(&mem.reg(d.a).as.b));
                    asmjit::Label one_label = a.new_label();
                    asmjit::Label continue_label = a.new_label();
                    a.cmp(src1_ptr, 0); // de Morgan law - a OR b = ~(~a AND ~b)
                    a.jne(one_label);
                    a.cmp(src2_ptr, 0);
                    a.jne(one_label);
                    a.mov(dst_ptr, 0);
                    a.jmp(continue_label);
                    a.bind(one_label);
                    a.mov(dst_ptr, 1);
                    a.bind(continue_label);
                    break;
                }
                */

                case bc::Opcode::JMP: {
                    auto d = bc::decodeAsBx(dw);
                    if (is_logging) {
                        printf("JUMP; %d\n", d.sbx);
                    }
                    int32_t dst = i + d.sbx + 1;
                    if (dst >= 0 && static_cast<uint32_t>(dst) < size) {
                        a.jmp(jmp_labels[dst]);
                    } else {
                        asmjit::x86::Mem pc_ptr = asmjit::x86::ptr_32(reinterpret_cast<uintptr_t>(&fr.pc));
                        a.add(pc_ptr, d.sbx);
                    }
                    break;
                }

                case bc::Opcode::JMP_T: {
                    auto d = bc::decodeAsBx(dw);
                    if (is_logging) {
                        printf("JUMP_TRUE r%d; %d\n", d.a, d.sbx);
                    }
                    int32_t dst = i + d.sbx + 1;
                    asmjit::x86::Mem src_ptr = asmjit::x86::ptr_8(reinterpret_cast<uintptr_t>(&mem.reg(d.a).as.b));
                    asmjit::Label one_label = a.new_label();
                    asmjit::Label continue_label = a.new_label();
                    a.cmp(src_ptr, 0);
                    a.jne(one_label);
                    a.jmp(continue_label);
                    a.bind(one_label);
                    if (dst >= 0 && static_cast<uint32_t>(dst) < size) {
                        a.jmp(jmp_labels[dst]);
                    } else {
                        asmjit::x86::Mem pc_ptr = asmjit::x86::ptr_32(reinterpret_cast<uintptr_t>(&fr.pc));
                        a.add(pc_ptr, d.sbx);
                    }
                    a.bind(continue_label);
                    break;
                }

                case bc::Opcode::JMP_F: {
                    auto d = bc::decodeAsBx(dw);
                    if (is_logging) {
                        printf("JUMP_FALSE r%d; %d\n", d.a, d.sbx);
                    }
                    asmjit::x86::Mem src_ptr = asmjit::x86::ptr_8(reinterpret_cast<uintptr_t>(&mem.reg(d.a).as.b));
                    int32_t dst = i + d.sbx + 1;
                    asmjit::Label zero_label = a.new_label();
                    asmjit::Label continue_label = a.new_label();
                    a.cmp(src_ptr, 0);
                    a.je(zero_label);
                    a.jmp(continue_label);
                    a.bind(zero_label);
                    if (dst >= 0 && static_cast<uint32_t>(dst) < size) {
                        a.jmp(jmp_labels[dst]);
                    } else {
                        asmjit::x86::Mem pc_ptr = asmjit::x86::ptr_32(reinterpret_cast<uintptr_t>(&fr.pc));
                        a.add(pc_ptr, d.sbx);
                    }
                    a.bind(continue_label);
                    break;
                }

                case bc::Opcode::I2F: {
                    auto d = bc::decodeABC(dw);
                    if (is_logging) {
                        printf("INT_TO_FLOAT r%d <- r%d\n", d.a, d.b);
                    }
                    if (const_folding && const_regs.contains(d.b)) {
                        int32_t src = const_regs[d.b].as.i;
                        asmjit::x86::Mem dst_v_ptr = asmjit::x86::ptr_8(reinterpret_cast<uintptr_t>(&mem.reg(d.a).as.f));
                        asmjit::x86::Mem dst_t_ptr = asmjit::x86::ptr_8(reinterpret_cast<uintptr_t>(&mem.reg(d.a).tag));
                        a.mov(dst_v_ptr, static_cast<float>(src));
                        a.mov(dst_t_ptr, 2);
                        const_regs[d.a] = vm::Value::f32(src);
                    } else {
                        const_regs.erase(d.a);
                        asmjit::x86::Mem src_ptr = asmjit::x86::ptr_32(reinterpret_cast<uintptr_t>(&mem.reg(d.b).as.i));
                        asmjit::x86::Mem dst_v_ptr = asmjit::x86::ptr_8(reinterpret_cast<uintptr_t>(&mem.reg(d.a).as.f));
                        asmjit::x86::Mem dst_t_ptr = asmjit::x86::ptr_8(reinterpret_cast<uintptr_t>(&mem.reg(d.a).tag));
                        a.cvtsi2ss(asmjit::x86::xmm0,src_ptr); // ConVerT Signed Integer to Scalar Single precision float
                        a.movd(dst_v_ptr, asmjit::x86::xmm0);
                        a.mov(dst_t_ptr, 2);
                    }
                    break;
                }

                case bc::Opcode::NEW_ARR: { // does it even work? i don't know
                    auto d = bc::decodeABC(dw);
                    if (is_logging) {
                        printf("NEW_ARRAY r%d <- [r%d]\n", d.a, d.b);
                    }
                    const_regs.erase(d.a);
                    asmjit::Imm src_ptr = reinterpret_cast<uintptr_t>(&mem.reg(d.b).as.i);
                    asmjit::x86::Mem dst_v_ptr = asmjit::x86::ptr_64(reinterpret_cast<uintptr_t>(&mem.reg(d.a).as.obj));
                    asmjit::x86::Mem dst_t_ptr = asmjit::x86::ptr_64(reinterpret_cast<uintptr_t>(&mem.reg(d.a).tag));
                    asmjit::Imm mem_ptr = reinterpret_cast<uintptr_t>(&mem);
                    a.mov(asmjit::x86::rcx, mem_ptr);
                    a.mov(asmjit::x86::rdx, src_ptr);
                    a.add(asmjit::x86::rsp, -128);
                    a.mov(asmjit::x86::r12, asmjit::x86::rsp);
                    a.and_(asmjit::x86::rsp, -16); // aligning before c++ function call
                    a.call(allocArrayWrapper);
                    a.mov(asmjit::x86::rsp, asmjit::x86::r12);
                    a.sub(asmjit::x86::rsp, -128);
                    a.mov(dst_v_ptr, asmjit::x86::rax);
                    a.mov(dst_t_ptr, 4);
                    break;
                }

                case bc::Opcode::GET_ELEM: {
                    auto d = bc::decodeABC(dw);
                    if (is_logging) {
                        printf("GET_ELEMENT r%d <- r%d[r%d]\n", d.a, d.b, d.c);
                    }
                    const_regs.erase(d.a);
                    if (const_folding && const_regs.contains(d.c)) {
                        auto arr = static_cast<vm::ObjArray*>(mem.reg(d.b).as.obj);
                        uintptr_t head = reinterpret_cast<uintptr_t>(arr->elems.data()); // may not work
                        int32_t id = const_regs[d.c].as.i;
                        uintptr_t p = head + id * 16;
                        asmjit::x86::Mem dst_ptr = asmjit::x86::ptr_128(reinterpret_cast<uintptr_t>(&mem.reg(d.a).tag));
                        a.movdqu(asmjit::x86::xmm0, asmjit::x86::ptr_128(p));
                        a.movdqu(dst_ptr, asmjit::x86::xmm0);
                    } else {
                        auto arr = static_cast<vm::ObjArray*>(mem.reg(d.b).as.obj);
                        uintptr_t head = reinterpret_cast<uintptr_t>(arr->elems.data()); // may not work
                        asmjit::x86::Mem id_ptr = asmjit::x86::ptr_64(reinterpret_cast<uintptr_t>(&mem.reg(d.c).as.i));
                        a.mov(asmjit::x86::rbx, id_ptr);
                        a.imul(asmjit::x86::rbx, 16);
                        a.add(asmjit::x86::rbx, head);
                        asmjit::x86::Mem src_ptr = asmjit::x86::ptr_128(asmjit::x86::rbx);
                        asmjit::x86::Mem dst_ptr = asmjit::x86::ptr_128(reinterpret_cast<uintptr_t>(&mem.reg(d.a).tag));
                        a.movdqu(asmjit::x86::xmm0, src_ptr);
                        a.movdqu(dst_ptr, asmjit::x86::xmm0);
                    }
                    break;
                }

                case bc::Opcode::SET_ELEM: {
                    auto d = bc::decodeABC(dw);
                    if (is_logging) {
                        printf("SET_ELEMENT r%d[r%d] <- r%d\n", d.a, d.b, d.c);
                    }
                    if (const_folding && const_regs.contains(d.b)) {
                        auto arr = static_cast<vm::ObjArray*>(mem.reg(d.a).as.obj);
                        uintptr_t head = reinterpret_cast<uintptr_t>(arr->elems.data()); // may not work
                        int32_t id = const_regs[d.b].as.i;
                        uintptr_t p = head + id * 16;
                        asmjit::x86::Mem src_ptr = asmjit::x86::ptr_128(reinterpret_cast<uintptr_t>(&mem.reg(d.c).tag));
                        a.movdqu(asmjit::x86::xmm0, src_ptr);
                        a.movdqu(asmjit::x86::ptr_128(p), asmjit::x86::xmm0);
                    } else {
                        auto arr = static_cast<vm::ObjArray*>(mem.reg(d.a).as.obj);
                        uintptr_t head = reinterpret_cast<uintptr_t>(arr->elems.data()); // may not work
                        asmjit::x86::Mem id_ptr = asmjit::x86::ptr_64(reinterpret_cast<uintptr_t>(&mem.reg(d.b).as.i));
                        a.mov(asmjit::x86::rbx, id_ptr);
                        a.imul(asmjit::x86::rbx, 16);
                        a.add(asmjit::x86::rbx, head);
                        asmjit::x86::Mem src_ptr = asmjit::x86::ptr_128(reinterpret_cast<uintptr_t>(&mem.reg(d.c).tag));
                        asmjit::x86::Mem dst_ptr = asmjit::x86::ptr_128(asmjit::x86::rbx);
                        a.movdqu(asmjit::x86::xmm0, src_ptr);
                        a.movdqu(dst_ptr, asmjit::x86::xmm0);
                    }
                    break;
                }

                case bc::Opcode::NEW_OBJ: {
                    auto d = bc::decodeABx(dw);
                    if (is_logging) {
                        printf("NEW_OBJECT r%d <- <c%d>\n", d.a, d.bx);
                    }
                    const_regs.erase(d.a);
                    auto* cr = static_cast<vm::ObjClassRef*>(fr.fn->constPool[d.bx].as.obj);
                    asmjit::Imm name_ptr = reinterpret_cast<uintptr_t>(cr->name);
                    asmjit::x86::Mem dst_v_ptr = asmjit::x86::ptr_64(reinterpret_cast<uintptr_t>(&mem.reg(d.a).as.obj));
                    asmjit::x86::Mem dst_t_ptr = asmjit::x86::ptr_64(reinterpret_cast<uintptr_t>(&mem.reg(d.a).tag));
                    asmjit::Imm mem_ptr = reinterpret_cast<uintptr_t>(&mem);
                    a.mov(asmjit::x86::rcx, mem_ptr);
                    a.mov(asmjit::x86::rdx, name_ptr);
                    a.add(asmjit::x86::rsp, -128);
                    a.mov(asmjit::x86::r12, asmjit::x86::rsp);
                    a.and_(asmjit::x86::rsp, -16); // aligning before c++ function call
                    a.call(allocInstanceWrapper);
                    a.mov(asmjit::x86::rsp, asmjit::x86::r12);
                    a.sub(asmjit::x86::rsp, -128);
                    a.mov(dst_v_ptr, asmjit::x86::rax);
                    a.mov(dst_t_ptr, 4);
                    break;
                }

                case bc::Opcode::GET_FIELD: {
                    auto d = bc::decodeABC(dw);
                    if (is_logging) {
                        printf("GET_CLASS_VARIABLE r%d <- r%d.<r%d>\n", d.a, d.b, d.c);
                    }
                    const_regs.erase(d.a);
                    auto inst = static_cast<vm::ObjInstance*>(mem.reg(d.b).as.obj);
                    auto fld = static_cast<vm::ObjFieldRef*>(mem.reg(d.c).as.obj);
                    asmjit::Imm g_fs_ptr = reinterpret_cast<uintptr_t>(&g_fieldSlots);
                    asmjit::Imm inst_ptr = reinterpret_cast<uintptr_t>(inst);
                    asmjit::Imm fld_ptr = reinterpret_cast<uintptr_t>(fld);
                    a.mov(asmjit::x86::rdx, g_fs_ptr);
                    a.mov(asmjit::x86::rdx, inst_ptr);
                    a.mov(asmjit::x86::r8, fld_ptr);
                    a.add(asmjit::x86::rsp, -128);
                    a.mov(asmjit::x86::r12, asmjit::x86::rsp);
                    a.and_(asmjit::x86::rsp, -16); // aligning before c++ function call
                    a.call(getFieldSlotAddress);
                    a.mov(asmjit::x86::rsp, asmjit::x86::r12);
                    a.sub(asmjit::x86::rsp, -128);
                    asmjit::x86::Mem dst_ptr = asmjit::x86::ptr_64(reinterpret_cast<uintptr_t>(&mem.reg(d.a).as.obj));
                    a.mov(dst_ptr, asmjit::x86::rax);
                    break;
                }

                case bc::Opcode::SET_FIELD: {
                    auto d = bc::decodeABC(dw);
                    if (is_logging) {
                        printf("SET_CLASS_VARIABLE r%d.<r%d> <- r%d\n", d.a, d.b, d.c);
                    }
                    auto inst = static_cast<vm::ObjInstance*>(mem.reg(d.a).as.obj);
                    auto fld = static_cast<vm::ObjFieldRef*>(mem.reg(d.b).as.obj);
                    asmjit::Imm g_fs_ptr = reinterpret_cast<uintptr_t>(&g_fieldSlots);
                    asmjit::Imm inst_ptr = reinterpret_cast<uintptr_t>(inst);
                    asmjit::Imm fld_ptr = reinterpret_cast<uintptr_t>(fld);
                    a.mov(asmjit::x86::rdx, g_fs_ptr);
                    a.mov(asmjit::x86::rdx, inst_ptr);
                    a.mov(asmjit::x86::r8, fld_ptr);
                    a.add(asmjit::x86::rsp, -128);
                    a.mov(asmjit::x86::r12, asmjit::x86::rsp);
                    a.and_(asmjit::x86::rsp, -16); // aligning before c++ function call
                    a.call(getFieldSlotAddress);
                    a.mov(asmjit::x86::rsp, asmjit::x86::r12);
                    a.sub(asmjit::x86::rsp, -128);
                    asmjit::x86::Mem src_ptr = asmjit::x86::ptr_128(reinterpret_cast<uintptr_t>(&mem.reg(d.c).tag));
                    asmjit::x86::Mem dst_ptr = asmjit::x86::ptr_128(asmjit::x86::rax);
                    a.movdqu(asmjit::x86::xmm0, src_ptr);
                    a.movdqu(dst_ptr, asmjit::x86::xmm0);
                    break;
                }

                case bc::Opcode::CALL: {
                    auto d = bc::decodeABC(dw);
                    if (is_logging) {
                        printf("CALL r%d <- r%d(%d)\n", d.a, d.b, d.c);
                    }
                    auto* fref = static_cast<vm::ObjFuncRef*>(mem.reg(d.b).as.obj);
                    uint8_t returnDst = d.a;
                    uint32_t argc = d.c;
                    asmjit::Imm fref_ptr = reinterpret_cast<uintptr_t>(fref);
                    asmjit::Imm fnByN_ptr = reinterpret_cast<uintptr_t>(&fnByName_);
                    asmjit::Imm mem_ptr = reinterpret_cast<uintptr_t>(&mem);
                    a.mov(asmjit::x86::rcx, fnByN_ptr);
                    a.mov(asmjit::x86::rdx, mem_ptr);
                    a.mov(asmjit::x86::r8, returnDst);
                    a.mov(asmjit::x86::r9, fref_ptr);
                    a.push(argc);
                    a.add(asmjit::x86::rsp, -128);
                    a.mov(asmjit::x86::r12, asmjit::x86::rsp);
                    a.and_(asmjit::x86::rsp, -16); // aligning before c++ function call
                    a.call(call_function);
                    a.mov(asmjit::x86::rsp, asmjit::x86::r12);
                    a.sub(asmjit::x86::rsp, -128);
                    a.add(asmjit::x86::rsp, 8);
                    break;
                }

                case bc::Opcode::CALLK: {
                    auto d = bc::decodeABx(dw);
                    if (is_logging) {
                        printf("CALL_CONST r%d <- c%d(...)\n", d.a, d.bx);
                    }
                    auto* fref = static_cast<vm::ObjFuncRef*>(fr.fn->constPool[d.bx].as.obj);
                    uint8_t returnDst = d.a;
                    uint32_t argc = fref->arity;
                    asmjit::Imm fref_ptr = reinterpret_cast<uintptr_t>(fref);
                    asmjit::Imm fnByN_ptr = reinterpret_cast<uintptr_t>(&fnByName_);
                    asmjit::Imm mem_ptr = reinterpret_cast<uintptr_t>(&mem);
                    a.mov(asmjit::x86::rcx, fnByN_ptr);
                    a.mov(asmjit::x86::rdx, mem_ptr);
                    a.mov(asmjit::x86::r8, returnDst);
                    a.mov(asmjit::x86::r9, fref_ptr);
                    a.push(argc);
                    a.add(asmjit::x86::rsp, -128);
                    a.mov(asmjit::x86::r12, asmjit::x86::rsp);
                    a.and_(asmjit::x86::rsp, -16); // aligning before c++ function call
                    a.call(call_function);
                    a.mov(asmjit::x86::rsp, asmjit::x86::r12);
                    a.sub(asmjit::x86::rsp, -128);
                    a.add(asmjit::x86::rsp, 8);
                    break;
                }

                case bc::Opcode::CALL_NATIVE: {
                    auto d = bc::decodeABC(dw);
                    if (is_logging) {
                        printf("CALL_NATIVE r%d <- ochev%d(%d)\n", d.a, d.b, d.c);
                    }
                    const_regs.erase(d.a);
                    uint16_t dest = d.a;
                    uint32_t nativeId = d.b;
                    uint32_t argc = d.c;
                    asmjit::Imm mem_ptr = reinterpret_cast<uintptr_t>(&mem);
                    a.mov(asmjit::x86::rcx, mem_ptr);
                    a.mov(asmjit::x86::rdx, nativeId);
                    a.mov(asmjit::x86::r8, argc);
                    a.mov(asmjit::x86::r9, dest);
                    a.add(asmjit::x86::rsp, -128);
                    a.mov(asmjit::x86::r12, asmjit::x86::rsp);
                    a.and_(asmjit::x86::rsp, -16); // aligning before c++ function call
                    a.call(callNative);
                    a.mov(asmjit::x86::rsp, asmjit::x86::r12);
                    a.sub(asmjit::x86::rsp, -128);
                    break;
                }

                case bc::Opcode::RET: {
                    auto d = bc::decodeABC(dw);
                    if (is_logging) {
                        if (d.a == 255) {
                            printf("RETURN\n");
                        } else {
                            printf("RETURN r%d\n", d.a);
                        }
                    }
                    asmjit::Imm mem_ptr = reinterpret_cast<uintptr_t>(&mem);
                    a.mov(asmjit::x86::rcx, mem_ptr);
                    a.mov(asmjit::x86::rdx, entry_ret_ptr);
                    a.mov(asmjit::x86::r8, end_flag);
                    a.mov(asmjit::x86::r9, d.a);
                    a.add(asmjit::x86::rsp, -128);
                    a.mov(asmjit::x86::r12, asmjit::x86::rsp);
                    a.and_(asmjit::x86::rsp, -16); // aligning before c++ function call
                    a.call(return_from_function);
                    a.mov(asmjit::x86::rsp, asmjit::x86::r12);
                    a.sub(asmjit::x86::rsp, -128);
                    a.cmp(asmjit::x86::ptr_8(reinterpret_cast<uintptr_t>(end_flag)), 1);
                    a.je(instant_ret);
                }
            }
        }
        vm::Frame& fr = mem.currentFrame();
        asmjit::x86::Mem pc_ptr = asmjit::x86::ptr_32(reinterpret_cast<uintptr_t>(&fr.pc));
        a.add(pc_ptr, size);
        a.bind(instant_ret);
        a.ret();

        Func f;
        rt.add(&f, &code);
        return f;
    }
}
