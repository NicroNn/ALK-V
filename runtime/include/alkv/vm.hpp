#pragma once
#include <vector>
#include <unordered_map>
#include <string>

#include "alkv/vm/memory.hpp"
#include "alkv/bytecode/bytecode.hpp"
#include "alkv/bytecode/loader.hpp"

namespace alkv::vm {

class VM {
public:
    VMMemory mem;

    void loadModule(const std::vector<bc::LoadedFunction>& fns);

    // entry run (с поддержкой CALLK)
    Value run(const std::string& entryName, const std::vector<Value>& args, bool is_compiling);

private:
    std::unordered_map<std::string, const bc::LoadedFunction*> fnByName_;

    static int32_t asInt(const Value& v);
    static float   asFloat(const Value& v);
    static bool    asBool(const Value& v);

    static bc::DecodedABC  abc(uint32_t w)  { return bc::decodeABC(w); }
    static bc::DecodedABx  abx(uint32_t w)  { return bc::decodeABx(w); }
    static bc::DecodedAsBx asbx(uint32_t w) { return bc::decodeAsBx(w); }
};

} // namespace alkv::vm