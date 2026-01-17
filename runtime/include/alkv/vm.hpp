#pragma once
#include <vector>
#include "alkv/vm/memory.hpp"
#include "alkv/bytecode/bytecode.hpp"

namespace alkv::vm {

class VM {
public:
    VMMemory mem;

    // args кладутся в R0..R(args-1)
    // Возвращает Value::nil() при RET void.
    Value run(const bc::Function& fn, const std::vector<Value>& args);

private:
    static int32_t asInt(const Value& v);
    static float   asFloat(const Value& v);
    static bool    asBool(const Value& v);
};

} // namespace alkv::vm