#pragma once
#include <string_view>
#include "alkv/vm/object.hpp"

namespace alkv::vm {

class Heap {
public:
    ~Heap() { freeAll(); }

    ObjString* allocString(std::string_view sv) {
        ObjString* s = ObjString::create(sv);
        linkObject(s);
        return s;
    }

    ObjArray* allocArray(std::size_t n) {
        ObjArray* a = ObjArray::create(n);
        linkObject(a);
        return a;
    }

    ObjInstance* allocInstance(ObjString* cls) {
        ObjInstance* o = ObjInstance::create(cls);
        linkObject(o);
        return o;
    }

    ObjFuncRef* allocFuncRef(ObjString* name, uint32_t arity) {
        ObjFuncRef* fr = ObjFuncRef::create(name, arity);
        linkObject(fr);
        return fr;
    }

    ObjClassRef* allocClassRef(ObjString* name) {
        ObjClassRef* cr = ObjClassRef::create(name);
        linkObject(cr);
        return cr;
    }

    ObjFieldRef* allocFieldRef(ObjString* cls, ObjString* field) {
        ObjFieldRef* fr = ObjFieldRef::create(cls, field);
        linkObject(fr);
        return fr;
    }

    // ---- GC hooks ----
    void mark(Obj* o) {
        if (!o || o->marked) return;
        o->marked = true;

        switch (o->type) {
            case ObjType::String:
                break;

            case ObjType::Array: {
                auto* a = static_cast<ObjArray*>(o);
                for (const auto& v : a->elems) markValue(v);
                break;
            }

            case ObjType::Instance: {
                auto* ins = static_cast<ObjInstance*>(o);
                mark(ins->className);
                for (const auto& v : ins->fields) markValue(v);
                break;
            }

            case ObjType::FuncRef: {
                auto* fr = static_cast<ObjFuncRef*>(o);
                mark(fr->name);
                break;
            }

            case ObjType::ClassRef: {
                auto* cr = static_cast<ObjClassRef*>(o);
                mark(cr->name);
                break;
            }

            case ObjType::FieldRef: {
                auto* fr = static_cast<ObjFieldRef*>(o);
                mark(fr->className);
                mark(fr->fieldName);
                break;
            }

            default:
                break;
        }
    }

    void sweep() {
        Obj* prev = nullptr;
        Obj* cur = objects_;
        while (cur) {
            if (cur->marked) {
                cur->marked = false;
                prev = cur;
                cur = cur->next;
                continue;
            }
            Obj* dead = cur;
            cur = cur->next;
            if (prev) prev->next = cur;
            else objects_ = cur;
            destroyObject(dead);
        }
    }

private:
    Obj* objects_ = nullptr;

    void linkObject(Obj* o) { o->next = objects_; objects_ = o; }

    void markValue(const Value& v) {
        if (v.tag == ValueTag::Obj && v.as.obj) mark(v.as.obj);
    }

    static void destroyObject(Obj* o) {
        switch (o->type) {
            case ObjType::String: {
                auto* s = static_cast<ObjString*>(o);
                s->~ObjString();
                ::operator delete((void*)s, std::align_val_t(alignof(ObjString)));
                break;
            }
            case ObjType::Array: {
                auto* a = static_cast<ObjArray*>(o);
                a->~ObjArray();
                ::operator delete((void*)a, std::align_val_t(alignof(ObjArray)));
                break;
            }
            case ObjType::Instance: {
                auto* i = static_cast<ObjInstance*>(o);
                i->~ObjInstance();
                ::operator delete((void*)i, std::align_val_t(alignof(ObjInstance)));
                break;
            }
            case ObjType::FuncRef: {
                auto* fr = static_cast<ObjFuncRef*>(o);
                fr->~ObjFuncRef();
                ::operator delete((void*)fr, std::align_val_t(alignof(ObjFuncRef)));
                break;
            }
            case ObjType::ClassRef: {
                auto* cr = static_cast<ObjClassRef*>(o);
                cr->~ObjClassRef();
                ::operator delete((void*)cr, std::align_val_t(alignof(ObjClassRef)));
                break;
            }
            case ObjType::FieldRef: {
                auto* fr = static_cast<ObjFieldRef*>(o);
                fr->~ObjFieldRef();
                ::operator delete((void*)fr, std::align_val_t(alignof(ObjFieldRef)));
                break;
            }
            default:
                o->~Obj();
                ::operator delete((void*)o);
                break;
        }
    }

    void freeAll() {
        Obj* cur = objects_;
        while (cur) {
            Obj* next = cur->next;
            destroyObject(cur);
            cur = next;
        }
        objects_ = nullptr;
    }
};

} // namespace alkv::vm