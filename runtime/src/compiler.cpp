#include "alkv/compiler/compiler.hpp"
#include <cstdio>

using namespace alkv;
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
                   uint8_t returnDst, vm::ObjFuncRef* fref) {
    auto itf = (*fnByN).find(std::string(fref->name->view()));
    if (itf == (*fnByN).end()) throw std::runtime_error("CALL: unknown function");

    const bc::LoadedFunction* callee = itf->second;


    // подготовим новый фрейм
    int32_t returnPc = mem->currentFrame().pc + 1;

    mem->pushFrame(&callee->fn, callee->fn.regCount, returnPc, returnDst);

    // args already in R0..R(argc-1) by convention
    // caller уже положил их туда (в твоём компиляторе есть placeArgsIntoR0)
    // поэтому здесь ничего копировать не надо
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
        *entry_ret_ptr = ret;
        *end_flag = 1;
        return;
    }

    // restore caller pc and place return
    vm::Frame& caller = mem->currentFrame();
    caller.pc = returnPc;
    if (returnDst != 255) mem->reg(returnDst) = ret;
}

vm::Value callNative(vm::VMMemory* mem, uint32_t nativeId, uint32_t argc) {
    auto getArg = [&](uint32_t i) -> const vm::Value& {
        if (i >= argc) throw std::runtime_error("CALL_NATIVE: arg OOB");
        return mem->reg(static_cast<uint16_t>(i));
    };

    switch (nativeId) {
        case 1: { // ochev.Out(x)
            if (argc != 1) throw std::runtime_error("ochev.Out expects 1 arg");
            const vm::Value& v = getArg(0);
            // минимальный принтер
            if (v.tag == vm::ValueTag::Int) std::cout << v.as.i;
            else if (v.tag == vm::ValueTag::Float) std::cout << v.as.f;
            else if (v.tag == vm::ValueTag::Bool) std::cout << (v.as.b ? "true" : "false");
            else if (v.tag == vm::ValueTag::Nil) std::cout << "nil";
            else if (v.tag == vm::ValueTag::Obj && v.as.obj && v.as.obj->type == vm::ObjType::String)
                std::cout << static_cast<vm::ObjString*>(v.as.obj)->view();
            else
                std::cout << "<obj>";
            std::cout << "\n";
            return vm::Value::nil();
        }
        case 2: { // ochev.In() -> string
            if (argc != 0) throw std::runtime_error("ochev.In expects 0 args");
            std::string line;
            std::getline(std::cin, line);
            auto* s = mem->heap.allocString(line);
            return vm::Value::object(s);
        }
            // 3/4/5 можно расширить под твою реальную семантику
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
        asmjit::StringLogger logger;
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

        for (uint32_t i = 0; i < size; ++i) {
            vm::Frame& fr = mem.currentFrame();
            if (jmp_labels.contains(i)) {
                a.bind(jmp_labels[i]);
            }
            uint32_t dw = fr.fn->code[fr.pc + i];
            bc::Opcode op = bc::decodeOp(dw);
            switch (op) {
                case bc::Opcode::NOP: {
                    break;
                }

                case bc::Opcode::MOV: {
                    auto d = bc::decodeABC(dw);
                    asmjit::x86::Mem dst_v_ptr = asmjit::x86::ptr_64(reinterpret_cast<uintptr_t>(&mem.reg(d.a).as.obj));
                    asmjit::x86::Mem src_v_ptr = asmjit::x86::ptr_64(reinterpret_cast<uintptr_t>(&mem.reg(d.b).as.obj));
                    asmjit::x86::Mem dst_t_ptr = asmjit::x86::ptr_64(reinterpret_cast<uintptr_t>(&mem.reg(d.a).tag));
                    asmjit::x86::Mem src_t_ptr = asmjit::x86::ptr_64(reinterpret_cast<uintptr_t>(&mem.reg(d.b).tag));
                    a.mov(asmjit::x86::rbx, src_v_ptr);
                    a.mov(dst_v_ptr, asmjit::x86::rbx);
                    a.mov(asmjit::x86::rbx, src_t_ptr);
                    a.mov(dst_t_ptr, asmjit::x86::rbx);
                    break;
                }

                case bc::Opcode::LOADK: {
                    auto d = bc::decodeABx(dw);
                    asmjit::x86::Mem dst_v_ptr = asmjit::x86::ptr_64(reinterpret_cast<uintptr_t>(&mem.reg(d.a).as.obj));
                    asmjit::x86::Mem dst_t_ptr = asmjit::x86::ptr_64(reinterpret_cast<uintptr_t>(&mem.reg(d.a).tag));
                    asmjit::Imm c = fr.fn->constPool[d.bx].as.obj;
                    asmjit::Imm t = fr.fn->constPool[d.bx].tag;
                    a.mov(dst_v_ptr, c);
                    a.mov(dst_t_ptr, t);
                    break;
                }

                case bc::Opcode::ADD_I:
                case bc::Opcode::SUB_I:
                case bc::Opcode::MUL_I:
                case bc::Opcode::DIV_I:
                case bc::Opcode::MOD_I: {
                    auto d = bc::decodeABC(dw);
                    asmjit::x86::Mem src1_ptr = asmjit::x86::ptr_32(reinterpret_cast<uintptr_t>(&mem.reg(d.b).as.i));
                    asmjit::x86::Mem src2_ptr = asmjit::x86::ptr_32(reinterpret_cast<uintptr_t>(&mem.reg(d.c).as.i));
                    asmjit::x86::Mem dst_ptr = asmjit::x86::ptr_32(reinterpret_cast<uintptr_t>(&mem.reg(d.a).as.i));
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
                    a.mov(dst_ptr, temp);
                    break;
                }

                case bc::Opcode::ADD_F:
                case bc::Opcode::SUB_F:
                case bc::Opcode::MUL_F:
                case bc::Opcode::DIV_F:
                case bc::Opcode::MOD_F: {
                    auto d = bc::decodeABC(dw);
                    asmjit::x86::Mem src1_ptr = asmjit::x86::ptr_32(reinterpret_cast<uintptr_t>(&mem.reg(d.b).as.i));
                    asmjit::x86::Mem src2_ptr = asmjit::x86::ptr_32(reinterpret_cast<uintptr_t>(&mem.reg(d.c).as.i));
                    asmjit::x86::Mem dst_ptr = asmjit::x86::ptr_32(reinterpret_cast<uintptr_t>(&mem.reg(d.a).as.i));
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
                                    a.cvttss2si(asmjit::x86::eax, asmjit::x86::xmm0); // holy sh*t
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
                    a.movd(dst_ptr, asmjit::x86::xmm0);
                    break;
                }

                case bc::Opcode::LT_I: case bc::Opcode::LE_I:
                case bc::Opcode::GT_I: case bc::Opcode::GE_I:
                case bc::Opcode::EQ:   case bc::Opcode::NE:   {
                    auto d = bc::decodeABC(dw);
                    asmjit::x86::Mem src1_ptr = asmjit::x86::ptr_32(reinterpret_cast<uintptr_t>(&mem.reg(d.b).as.i));
                    asmjit::x86::Mem src2_ptr = asmjit::x86::ptr_32(reinterpret_cast<uintptr_t>(&mem.reg(d.c).as.i));
                    asmjit::x86::Mem dst_ptr = asmjit::x86::ptr_32(reinterpret_cast<uintptr_t>(&mem.reg(d.a).as.i));
                    a.mov(asmjit::x86::eax, src1_ptr);
                    a.cmp(asmjit::x86::eax, src2_ptr);
                    asmjit::Label true_label = a.new_label();
                    asmjit::Label continue_label = a.new_label();
                    switch (op) {
                        case bc::Opcode::LT_I: a.jb(true_label);  break;
                        case bc::Opcode::LE_I: a.jbe(true_label); break;
                        case bc::Opcode::GT_I: a.ja(true_label);  break;
                        case bc::Opcode::GE_I: a.jae(true_label); break;
                        case bc::Opcode::EQ:   a.je(true_label);  break;
                        case bc::Opcode::NE:   a.jne(true_label); break;
                        default: break;
                    }
                    a.mov(dst_ptr, 0);
                    a.jmp(continue_label);
                    a.bind(true_label);
                    a.mov(dst_ptr, 1);
                    a.bind(continue_label);
                    break;
                }

                case bc::Opcode::LT_F: case bc::Opcode::LE_F:
                case bc::Opcode::GT_F: case bc::Opcode::GE_F: {
                    auto d = bc::decodeABC(dw);
                    asmjit::x86::Mem src1_ptr = asmjit::x86::ptr_32(reinterpret_cast<uintptr_t>(&mem.reg(d.b).as.i));
                    asmjit::x86::Mem src2_ptr = asmjit::x86::ptr_32(reinterpret_cast<uintptr_t>(&mem.reg(d.c).as.i));
                    asmjit::x86::Mem dst_ptr = asmjit::x86::ptr_32(reinterpret_cast<uintptr_t>(&mem.reg(d.a).as.i));
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
                    a.mov(dst_ptr, 0);
                    a.jmp(continue_label);
                    a.bind(true_label);
                    a.mov(dst_ptr, 1);
                    a.bind(continue_label);
                    break;
                }

                case bc::Opcode::NOT: {
                    auto d = bc::decodeABC(dw);
                    asmjit::x86::Mem src_ptr = asmjit::x86::ptr_8(reinterpret_cast<uintptr_t>(&mem.reg(d.b).as.b));
                    asmjit::x86::Mem dst_ptr = asmjit::x86::ptr_8(reinterpret_cast<uintptr_t>(&mem.reg(d.a).as.b));
                    asmjit::Label one_label = a.new_label();
                    asmjit::Label continue_label = a.new_label();
                    a.cmp(src_ptr, 0);
                    a.je(one_label);
                    a.mov(dst_ptr, 0);
                    a.jmp(continue_label);
                    a.bind(one_label);
                    a.mov(dst_ptr, 1);
                    a.bind(continue_label);
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
                    asmjit::x86::Mem src_ptr = asmjit::x86::ptr_32(reinterpret_cast<uintptr_t>(&mem.reg(d.b).as.i));
                    asmjit::x86::Mem dst_ptr = asmjit::x86::ptr_32(reinterpret_cast<uintptr_t>(&mem.reg(d.a).as.i));
                    a.cvtsi2ss(asmjit::x86::xmm0, src_ptr); // ConVerT Signed Integer to Scalar Single precision float
                    a.movd(dst_ptr, asmjit::x86::xmm0);
                    break;
                }

                case bc::Opcode::NEW_ARR: { // does it even work? i don't know
                    auto d = bc::decodeABC(dw);
                    asmjit::x86::Mem src_ptr = asmjit::x86::ptr_64(reinterpret_cast<uintptr_t>(&mem.reg(d.b).as.i));
                    asmjit::x86::Mem dst_ptr = asmjit::x86::ptr_64(reinterpret_cast<uintptr_t>(&mem.reg(d.a).as.obj));
                    asmjit::x86::Mem mem_ptr = asmjit::x86::ptr_64(reinterpret_cast<uintptr_t>(&mem));
                    a.mov(asmjit::x86::rcx, mem_ptr);
                    a.mov(asmjit::x86::rdx, src_ptr);
                    a.add(asmjit::x86::rsp, -128);
                    a.mov(asmjit::x86::r12, asmjit::x86::rsp);
                    a.and_(asmjit::x86::rsp, -16); // aligning before c++ function call
                    a.call(allocArrayWrapper);
                    a.mov(asmjit::x86::rsp, asmjit::x86::r12);
                    a.sub(asmjit::x86::rsp, -128);
                    a.mov(dst_ptr, asmjit::x86::rax);
                    break;
                }

                case bc::Opcode::GET_ELEM: {
                    auto d = bc::decodeABC(dw);
                    auto arr = static_cast<vm::ObjArray*>(mem.reg(d.b).as.obj);
                    uintptr_t head = reinterpret_cast<uintptr_t>(arr->elems.data()); // may not work
                    asmjit::x86::Mem id_ptr = asmjit::x86::ptr_64(reinterpret_cast<uintptr_t>(&mem.reg(d.c).as.i));
                    a.mov(asmjit::x86::rbx, id_ptr);
                    a.imul(asmjit::x86::rbx, 8);
                    a.add(asmjit::x86::rbx, head);
                    asmjit::x86::Mem src_ptr = asmjit::x86::ptr_64(asmjit::x86::rbx);
                    asmjit::x86::Mem dst_ptr = asmjit::x86::ptr_64(reinterpret_cast<uintptr_t>(&mem.reg(d.a).as.obj));
                    a.mov(asmjit::x86::rbx, src_ptr);
                    a.mov(dst_ptr, asmjit::x86::rbx);
                    break;
                }

                case bc::Opcode::SET_ELEM: {
                    auto d = bc::decodeABC(dw);
                    auto arr = static_cast<vm::ObjArray*>(mem.reg(d.a).as.obj);
                    uintptr_t head = reinterpret_cast<uintptr_t>(arr->elems.data()); // may not work
                    asmjit::x86::Mem id_ptr = asmjit::x86::ptr_64(reinterpret_cast<uintptr_t>(&mem.reg(d.b).as.i));
                    a.mov(asmjit::x86::rbx, id_ptr);
                    a.imul(asmjit::x86::rbx, 8);
                    a.add(asmjit::x86::rbx, head);
                    asmjit::x86::Mem src_ptr = asmjit::x86::ptr_64(reinterpret_cast<uintptr_t>(&mem.reg(d.c).as.obj));
                    asmjit::x86::Mem dst_ptr = asmjit::x86::ptr_64(asmjit::x86::rbx);
                    a.mov(asmjit::x86::rbx, src_ptr);
                    a.mov(dst_ptr, asmjit::x86::rbx);
                    break;
                }

                case bc::Opcode::NEW_OBJ: {
                    auto d = bc::decodeABx(dw);
                    auto* cr = static_cast<vm::ObjClassRef*>(fr.fn->constPool[d.bx].as.obj);
                    asmjit::Imm name_ptr = reinterpret_cast<uintptr_t>(cr->name);
                    asmjit::x86::Mem dst_ptr = asmjit::x86::ptr_64(reinterpret_cast<uintptr_t>(&mem.reg(d.a).as.obj));
                    asmjit::x86::Mem mem_ptr = asmjit::x86::ptr_64(reinterpret_cast<uintptr_t>(&mem));
                    a.mov(asmjit::x86::rcx, mem_ptr);
                    a.mov(asmjit::x86::rdx, name_ptr);
                    a.add(asmjit::x86::rsp, -128);
                    a.mov(asmjit::x86::r12, asmjit::x86::rsp);
                    a.and_(asmjit::x86::rsp, -16); // aligning before c++ function call
                    a.call(allocInstanceWrapper);
                    a.mov(asmjit::x86::rsp, asmjit::x86::r12);
                    a.sub(asmjit::x86::rsp, -128);
                    a.mov(dst_ptr, asmjit::x86::rax);
                    break;
                }

                case bc::Opcode::GET_FIELD: {
                    auto d = bc::decodeABC(dw);
                    auto inst = static_cast<vm::ObjInstance*>(mem.reg(d.b).as.obj);
                    auto fld = static_cast<vm::ObjFieldRef*>(mem.reg(d.c).as.obj);
                    asmjit::x86::Mem g_fs_ptr = asmjit::x86::ptr_64(reinterpret_cast<uintptr_t>(&g_fieldSlots));
                    asmjit::x86::Mem inst_ptr = asmjit::x86::ptr_64(reinterpret_cast<uintptr_t>(inst));
                    asmjit::x86::Mem fld_ptr = asmjit::x86::ptr_64(reinterpret_cast<uintptr_t>(fld));
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
                    auto inst = static_cast<vm::ObjInstance*>(mem.reg(d.a).as.obj);
                    auto fld = static_cast<vm::ObjFieldRef*>(mem.reg(d.b).as.obj);
                    asmjit::x86::Mem g_fs_ptr = asmjit::x86::ptr_64(reinterpret_cast<uintptr_t>(&g_fieldSlots));
                    asmjit::x86::Mem inst_ptr = asmjit::x86::ptr_64(reinterpret_cast<uintptr_t>(inst));
                    asmjit::x86::Mem fld_ptr = asmjit::x86::ptr_64(reinterpret_cast<uintptr_t>(fld));
                    a.mov(asmjit::x86::rdx, g_fs_ptr);
                    a.mov(asmjit::x86::rdx, inst_ptr);
                    a.mov(asmjit::x86::r8, fld_ptr);
                    a.add(asmjit::x86::rsp, -128);
                    a.mov(asmjit::x86::r12, asmjit::x86::rsp);
                    a.and_(asmjit::x86::rsp, -16); // aligning before c++ function call
                    a.call(getFieldSlotAddress);
                    a.mov(asmjit::x86::rsp, asmjit::x86::r12);
                    a.sub(asmjit::x86::rsp, -128);
                    asmjit::x86::Mem src_ptr = asmjit::x86::ptr_64(reinterpret_cast<uintptr_t>(&mem.reg(d.c).as.obj));
                    asmjit::x86::Mem dst_ptr = asmjit::x86::ptr_64(asmjit::x86::rax);
                    a.mov(asmjit::x86::rbx, src_ptr);
                    a.mov(dst_ptr, asmjit::x86::rbx);
                    break;
                }

                case bc::Opcode::CALL: {
                    auto d = bc::decodeABC(dw);
                    auto* fref = static_cast<vm::ObjFuncRef*>(mem.reg(d.b).as.obj);
                    uint8_t returnDst = d.a;
                    asmjit::x86::Mem fref_ptr = asmjit::x86::ptr_64(reinterpret_cast<uintptr_t>(fref));
                    asmjit::x86::Mem fnByN_ptr = asmjit::x86::ptr_64(reinterpret_cast<uintptr_t>(&fnByName_));
                    asmjit::x86::Mem mem_ptr = asmjit::x86::ptr_64(reinterpret_cast<uintptr_t>(&mem));
                    a.mov(asmjit::x86::rcx, fnByN_ptr);
                    a.mov(asmjit::x86::rdx, mem_ptr);
                    a.mov(asmjit::x86::r8, returnDst);
                    a.mov(asmjit::x86::r9, fref_ptr);
                    a.add(asmjit::x86::rsp, -128);
                    a.mov(asmjit::x86::r12, asmjit::x86::rsp);
                    a.and_(asmjit::x86::rsp, -16); // aligning before c++ function call
                    a.call(call_function);
                    a.mov(asmjit::x86::rsp, asmjit::x86::r12);
                    a.sub(asmjit::x86::rsp, -128);
                    break;
                }

                case bc::Opcode::CALLK: {
                    auto d = bc::decodeABx(dw);
                    auto* fref = static_cast<vm::ObjFuncRef*>(fr.fn->constPool[d.bx].as.obj);
                    uint8_t returnDst = d.a;
                    asmjit::x86::Mem fref_ptr = asmjit::x86::ptr_64(reinterpret_cast<uintptr_t>(fref));
                    asmjit::x86::Mem fnByN_ptr = asmjit::x86::ptr_64(reinterpret_cast<uintptr_t>(&fnByName_));
                    asmjit::x86::Mem mem_ptr = asmjit::x86::ptr_64(reinterpret_cast<uintptr_t>(&mem));
                    a.mov(asmjit::x86::rcx, fnByN_ptr);
                    a.mov(asmjit::x86::rdx, mem_ptr);
                    a.mov(asmjit::x86::r8, returnDst);
                    a.mov(asmjit::x86::r9, fref_ptr);
                    a.add(asmjit::x86::rsp, -128);
                    a.mov(asmjit::x86::r12, asmjit::x86::rsp);
                    a.and_(asmjit::x86::rsp, -16); // aligning before c++ function call
                    a.call(call_function);
                    a.mov(asmjit::x86::rsp, asmjit::x86::r12);
                    a.sub(asmjit::x86::rsp, -128);
                    break;
                }

                case bc::Opcode::CALL_NATIVE: {
                    auto d = bc::decodeABC(dw);
                    uint32_t nativeId = d.b;
                    uint32_t argc = d.c;
                    asmjit::x86::Mem mem_ptr = asmjit::x86::ptr_64(reinterpret_cast<uintptr_t>(&mem));
                    a.mov(asmjit::x86::rcx, mem_ptr);
                    a.mov(asmjit::x86::rdx, nativeId);
                    a.mov(asmjit::x86::r8, argc);
                    a.add(asmjit::x86::rsp, -128);
                    a.mov(asmjit::x86::r12, asmjit::x86::rsp);
                    a.and_(asmjit::x86::rsp, -16); // aligning before c++ function call
                    a.call(callNative);
                    a.mov(asmjit::x86::rsp, asmjit::x86::r12);
                    a.sub(asmjit::x86::rsp, -128);
                    asmjit::x86::Mem dst_ptr = asmjit::x86::ptr_64(reinterpret_cast<uintptr_t>(&mem.reg(d.a).as.obj));
                    a.mov(dst_ptr, asmjit::x86::rax);
                    break;
                }

                case bc::Opcode::RET: {
                    auto d = bc::decodeABC(dw);
                    std::cout << nullptr << ' ' << (size_t)(&mem) << std::endl;
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

        if (is_logging) {
            printf("Logging data: \n%s\n", logger.data());
        }

        Func f;
        rt.add(&f, &code);
        return f;
    }
}
