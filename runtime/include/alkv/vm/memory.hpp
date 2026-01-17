#pragma once
#include <vector>
#include <cstdint>
#include <cassert>
#include "alkv/vm/value.hpp"
#include "alkv/vm/heap.hpp"

namespace alkv::vm {

struct Frame {
    uint32_t base = 0;      // base index in valueStack
    uint16_t regCount = 0;  // number of registers
};

class VMMemory {
public:
    Heap heap;

    std::vector<Value> constPool;   // runtime constants (roots)
    std::vector<Value> valueStack;  // registers of all frames
    std::vector<Frame> callStack;   // call frames

    std::size_t pushFrame(uint16_t regCount) {
        Frame f;
        f.base = static_cast<uint32_t>(valueStack.size());
        f.regCount = regCount;

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

    Frame& currentFrame() {
        assert(!callStack.empty());
        return callStack.back();
    }

    Value& reg(uint16_t idx) {
        Frame& f = currentFrame();
        assert(idx < f.regCount);
        return valueStack[f.base + idx];
    }

    const Value& reg(uint16_t idx) const {
        assert(!callStack.empty());
        const Frame& f = callStack.back();
        assert(idx < f.regCount);
        return valueStack[f.base + idx];
    }

    // ---- GC root traversal hook ----
    void markRoots() {
        // constants
        for (const auto& v : constPool) markValue(v);

        // all registers in all frames
        for (const Frame& fr : callStack) {
            for (uint32_t i = 0; i < fr.regCount; ++i) {
                markValue(valueStack[fr.base + i]);
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