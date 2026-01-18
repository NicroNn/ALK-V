#pragma once
#include <vector>
#include <cstdint>
#include <cassert>

#include "alkv/vm/value.hpp"
#include "alkv/vm/heap.hpp"
#include "alkv/bytecode/bytecode.hpp"

namespace alkv::vm {

struct Frame {
    uint32_t base = 0;
    uint16_t regCount = 0;

    const alkv::bc::Function* fn = nullptr;
    int32_t pc = 0;

    int32_t returnPc = -1;   // куда вернуться в caller
    uint8_t returnDst = 255; // в какой рег caller положить return (255 = игнор)
};

class VMMemory {
public:
    Heap heap;
    std::vector<Value> valueStack;
    std::vector<Frame> callStack;

    std::size_t pushFrame(const alkv::bc::Function* fn, uint16_t regCount, int32_t returnPc, uint8_t returnDst) {
        Frame f;
        f.base = static_cast<uint32_t>(valueStack.size());
        f.regCount = regCount;
        f.fn = fn;
        f.pc = 0;
        f.returnPc = returnPc;
        f.returnDst = returnDst;

        valueStack.resize(valueStack.size() + regCount, Value::nil());
        callStack.push_back(f);
        return callStack.size() - 1;
    }

    void popFrame() {
        assert(!callStack.empty());
        Frame f = callStack.back();
        callStack.pop_back();
        valueStack.resize(f.base);
    }

    Frame& currentFrame() { assert(!callStack.empty()); return callStack.back(); }
    const Frame& currentFrame() const { assert(!callStack.empty()); return callStack.back(); }

    Value& reg(uint16_t idx) {
        Frame& f = currentFrame();
        assert(idx < f.regCount);
        return valueStack[f.base + idx];
    }
    const Value& reg(uint16_t idx) const {
        const Frame& f = currentFrame();
        assert(idx < f.regCount);
        return valueStack[f.base + idx];
    }

    // ---- GC roots ----
    void markRoots() {
        // все regs во всех фреймах
        for (const Frame& fr : callStack) {
            for (uint32_t i = 0; i < fr.regCount; ++i) {
                markValue(valueStack[fr.base + i]);
            }
            // constPool текущей функции фрейма — тоже корни
            if (fr.fn) {
                for (const auto& v : fr.fn->constPool) markValue(v);
            }
        }
    }

private:
    void markValue(const Value& v) {
        if (v.tag == ValueTag::Obj && v.as.obj) {
            heap.mark(v.as.obj);
        }
    }
};

} // namespace alkv::vm