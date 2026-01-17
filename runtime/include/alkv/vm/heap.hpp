#pragma once
#include <string_view>
#include <utility>
#include "alkv/vm/object.hpp"

namespace alkv::vm {

class Heap {
public:
    Heap() = default;
    Heap(const Heap&) = delete;
    Heap& operator=(const Heap&) = delete;

    ~Heap() { freeAll(); }

    ObjString* allocString(std::string_view sv) {
        ObjString* s = ObjString::create(sv);
        linkObject(s);
        return s;
    }

    // ---- GC hooks ----
    void mark(Obj* o) {
        if (!o || o->marked) return;
        o->marked = true;

        // Trace references inside object:
        switch (o->type) {
            case ObjType::String:
                // no outgoing references
                break;
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

    Obj* objects() const { return objects_; }

private:
    Obj* objects_ = nullptr;

    void linkObject(Obj* o) {
        o->next = objects_;
        objects_ = o;
    }

    static void destroyObject(Obj* o) {
        switch (o->type) {
            case ObjType::String: {
                auto* s = static_cast<ObjString*>(o);
                s->~ObjString();
                ::operator delete((void*)s, std::align_val_t(alignof(ObjString)));
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