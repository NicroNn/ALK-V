#pragma once
#include <cstdint>
#include <cstddef>
#include <string_view>
#include <new>
#include <cstring>

namespace alkv::vm {

enum class ObjType : uint8_t {
    String,
    // future: Array, Function, Closure, Map, ...
};

struct Obj {
    ObjType type;
    bool marked = false;  // mark-bit for GC
    Obj* next = nullptr;  // intrusive heap list

    explicit Obj(ObjType t) : type(t) {}
    virtual ~Obj() = default;
};

struct ObjString final : Obj {
    uint32_t length = 0;

    ObjString() : Obj(ObjType::String) {}

    char* chars() { return reinterpret_cast<char*>(this + 1); }
    const char* chars() const { return reinterpret_cast<const char*>(this + 1); }

    std::string_view view() const { return std::string_view(chars(), length); }

    // allocate header + bytes + '\0' as a single block
    static ObjString* create(std::string_view sv) {
        std::size_t len = sv.size();
        std::size_t bytes = sizeof(ObjString) + len + 1;

        void* mem = ::operator new(bytes, std::align_val_t(alignof(ObjString)));
        auto* s = new (mem) ObjString();
        s->length = static_cast<uint32_t>(len);

        std::memcpy(s->chars(), sv.data(), len);
        s->chars()[len] = '\0';
        return s;
    }
};

} // namespace alkv::vm