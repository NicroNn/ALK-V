#pragma once
#include <cstdint>

namespace alkv::vm {

struct Obj; // forward

enum class ValueTag : uint8_t {
    Nil,
    Int,
    Float,
    Bool,
    Obj
};

struct Value {
    ValueTag tag{ValueTag::Nil};
    union {
        int32_t i;
        float   f;
        bool    b;
        Obj*    obj;
    } as{};

    static Value nil() { return Value{}; }
    static Value i32(int32_t v) { Value x; x.tag = ValueTag::Int; x.as.i = v; return x; }
    static Value f32(float v) { Value x; x.tag = ValueTag::Float; x.as.f = v; return x; }
    static Value boolean(bool v) { Value x; x.tag = ValueTag::Bool; x.as.b = v; return x; }
    static Value object(Obj* o) { Value x; x.tag = ValueTag::Obj; x.as.obj = o; return x; }

    bool isObj() const { return tag == ValueTag::Obj && as.obj != nullptr; }
};

} // namespace alkv::vm