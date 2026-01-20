#pragma once
#include <cstdint>
#include <cstddef>
#include <string_view>
#include <new>
#include <cstring>
#include <vector>

#include "alkv/vm/value.hpp"

namespace alkv::vm {

enum class ObjType : uint8_t {
    String,
    Array,       // vector<Value>
    Instance,    // className + fields
    FuncRef,     // name + arity
    ClassRef,    // name
    FieldRef,    // className + fieldName
    MethodRef    // className + methodName + arity
};

struct Obj {
    ObjType type;
    bool marked = false;
    Obj* next = nullptr;
    explicit Obj(ObjType t) : type(t) {}
    virtual ~Obj() = default;
    virtual size_t size() const = 0;  // Добавлен виртуальный метод
};

struct ObjString final : Obj {
    uint32_t length = 0;
    ObjString() : Obj(ObjType::String) {}
    
    size_t size() const override {
        return sizeof(ObjString) + length + 1;  // +1 для нулевого байта
    }

    char* chars() { return reinterpret_cast<char*>(this + 1); }
    const char* chars() const { return reinterpret_cast<const char*>(this + 1); }
    std::string_view view() const { return std::string_view(chars(), length); }

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

struct ObjArray final : Obj {
    std::vector<Value> elems;
    explicit ObjArray(std::size_t n) : Obj(ObjType::Array), elems(n, Value::nil()) {}
    
    size_t size() const override {
        return sizeof(ObjArray) + elems.capacity() * sizeof(Value);
    }
    
    static ObjArray* create(std::size_t n) {
        void* mem = ::operator new(sizeof(ObjArray), std::align_val_t(alignof(ObjArray)));
        return new (mem) ObjArray(n);
    }
};

struct ObjInstance final : Obj {
    ObjString* className = nullptr;
    std::vector<Value> fields;
    
    explicit ObjInstance(ObjString* cls) : Obj(ObjType::Instance), className(cls) {}
    
    size_t size() const override {
        return sizeof(ObjInstance) + fields.capacity() * sizeof(Value);
    }
    
    static ObjInstance* create(ObjString* cls) {
        void* mem = ::operator new(sizeof(ObjInstance), std::align_val_t(alignof(ObjInstance)));
        return new (mem) ObjInstance(cls);
    }
};

struct ObjFuncRef final : Obj {
    ObjString* name = nullptr;
    uint32_t arity = 0;
    
    ObjFuncRef(ObjString* n, uint32_t a) : Obj(ObjType::FuncRef), name(n), arity(a) {}
    
    size_t size() const override {
        return sizeof(ObjFuncRef);
    }
    
    static ObjFuncRef* create(ObjString* n, uint32_t a) {
        void* mem = ::operator new(sizeof(ObjFuncRef), std::align_val_t(alignof(ObjFuncRef)));
        return new (mem) ObjFuncRef(n, a);
    }
};

struct ObjClassRef final : Obj {
    ObjString* name = nullptr;
    
    explicit ObjClassRef(ObjString* n) : Obj(ObjType::ClassRef), name(n) {}
    
    size_t size() const override {
        return sizeof(ObjClassRef);
    }
    
    static ObjClassRef* create(ObjString* n) {
        void* mem = ::operator new(sizeof(ObjClassRef), std::align_val_t(alignof(ObjClassRef)));
        return new (mem) ObjClassRef(n);
    }
};

struct ObjFieldRef final : Obj {
    ObjString* className = nullptr;
    ObjString* fieldName = nullptr;
    
    ObjFieldRef(ObjString* c, ObjString* f) : Obj(ObjType::FieldRef), className(c), fieldName(f) {}
    
    size_t size() const override {
        return sizeof(ObjFieldRef);
    }
    
    static ObjFieldRef* create(ObjString* c, ObjString* f) {
        void* mem = ::operator new(sizeof(ObjFieldRef), std::align_val_t(alignof(ObjFieldRef)));
        return new (mem) ObjFieldRef(c, f);
    }
};

} // namespace alkv::vm