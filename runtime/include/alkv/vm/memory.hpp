#pragma once
#include <vector>
#include <cstdint>
#include <cassert>
#include <iostream>

#include "alkv/vm/value.hpp"
#include "alkv/vm/heap.hpp"
#include "alkv/bytecode/bytecode.hpp"

namespace alkv::vm {

struct Frame {
    uint32_t base = 0;
    uint16_t regCount = 0;

    const alkv::bc::Function* fn = nullptr;
    int32_t pc = 0;

    int32_t returnPc = -1;
    uint8_t returnDst = 255;
};

class VMMemory {
public:
    Heap heap;
    std::vector<Value> valueStack;
    std::vector<Frame> callStack;
    
    VMMemory() {
        heap.setLoggingEnabled(true);
    }
    
    // GC statistics
    struct GCStats {
        size_t totalCollections = 0;
        size_t totalBytesFreed = 0;
        size_t totalObjectsFreed = 0;
        size_t bytesAllocated = 0;
        size_t objectsCount = 0;
        size_t lastFreedBytes = 0;
        size_t lastFreedObjects = 0;
        
        void print() const {
            std::cout << "GC Statistics:\n";
            std::cout << "  Total collections: " << totalCollections << "\n";
            std::cout << "  Total bytes freed: " << totalBytesFreed << "\n";
            std::cout << "  Total objects freed: " << totalObjectsFreed << "\n";
            std::cout << "  Last collection freed: " << lastFreedBytes 
                      << " bytes, " << lastFreedObjects << " objects\n";
            std::cout << "  Currently allocated: " << bytesAllocated << " bytes\n";
            std::cout << "  Live objects: " << objectsCount << "\n";
        }
    };
    
    GCStats stats;

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

    void collectGarbage() {
        heap.collectGarbage([this](Heap& h) { this->markRoots(h); });
        updateStats();
    }
    
    void markRoots(Heap& h) {
        for (const Frame& fr : callStack) {
            for (uint32_t i = 0; i < fr.regCount; ++i) {
                const Value& v = valueStack[fr.base + i];
                if (v.isObj()) {
                    h.mark(v.as.obj);
                }
            }
            if (fr.fn) {
                for (const auto& v : fr.fn->constPool) {
                    if (v.isObj()) {
                        h.mark(v.as.obj);
                    }
                }
            }
        }
    }
    
    void forceGC() {
        collectGarbage();
    }
    
    void updateStats() {
        stats.totalCollections = heap.getCollections();
        stats.totalBytesFreed = heap.getTotalFreedBytes();
        stats.totalObjectsFreed = heap.getTotalFreedObjects();
        stats.bytesAllocated = heap.getBytesAllocated();
        stats.objectsCount = heap.getObjectsCount();
        stats.lastFreedBytes = heap.getLastFreedBytes();
        stats.lastFreedObjects = heap.getLastFreedObjects();
    }

private:
    void markValue(Heap& h, const Value& v) {
        if (v.isObj()) {
            h.mark(v.as.obj);
        }
    }
};

}