#pragma once
#include <string_view>
#include <vector>
#include <stack>
#include <functional>
#include <cstddef>
#include <iostream>
#include "alkv/vm/object.hpp"

namespace alkv::vm {

class Heap {
public:
    ~Heap() { freeAll(); }

    ObjString* allocString(std::string_view sv) {
        ObjString* s = ObjString::create(sv);
        linkObject(s);
        allocatedBytes += s->size();
        checkGC();
        return s;
    }

    ObjArray* allocArray(std::size_t n) {
        ObjArray* a = ObjArray::create(n);
        linkObject(a);
        allocatedBytes += a->size();
        checkGC();
        return a;
    }

    ObjInstance* allocInstance(ObjString* cls) {
        ObjInstance* o = ObjInstance::create(cls);
        linkObject(o);
        allocatedBytes += o->size();
        checkGC();
        return o;
    }

    ObjFuncRef* allocFuncRef(ObjString* name, uint32_t arity) {
        ObjFuncRef* fr = ObjFuncRef::create(name, arity);
        linkObject(fr);
        allocatedBytes += fr->size();
        checkGC();
        return fr;
    }

    ObjClassRef* allocClassRef(ObjString* name) {
        ObjClassRef* cr = ObjClassRef::create(name);
        linkObject(cr);
        allocatedBytes += cr->size();
        checkGC();
        return cr;
    }

    ObjFieldRef* allocFieldRef(ObjString* cls, ObjString* field) {
        ObjFieldRef* fr = ObjFieldRef::create(cls, field);
        linkObject(fr);
        allocatedBytes += fr->size();
        checkGC();
        return fr;
    }

    void collectGarbage(std::function<void(Heap&)> markRoots) {
        markAll(markRoots);
        sweep();
        
        nextGcThreshold = allocatedBytes * 2;
        collections++;
        
        if (enableLogging) {
            std::cout << "[GC] Collected " << lastFreedBytes << " bytes, "
                      << lastFreedObjects << " objects. Live: " 
                      << getObjectsCount() << " objects, " 
                      << allocatedBytes << " bytes\n";
        }
    }
    
    void mark(Obj* o) {
        if (!o) return;
        
        std::stack<Obj*> stack;
        stack.push(o);
        
        while (!stack.empty()) {
            Obj* current = stack.top();
            stack.pop();
            
            if (current->marked) continue;
            current->marked = true;
            
            switch (current->type) {
                case ObjType::String:
                    break;
                    
                case ObjType::Array: {
                    auto* a = static_cast<ObjArray*>(current);
                    for (const auto& v : a->elems) {
                        if (v.isObj()) {
                            stack.push(v.as.obj);
                        }
                    }
                    break;
                }
                    
                case ObjType::Instance: {
                    auto* ins = static_cast<ObjInstance*>(current);
                    if (ins->className) stack.push(ins->className);
                    for (const auto& v : ins->fields) {
                        if (v.isObj()) {
                            stack.push(v.as.obj);
                        }
                    }
                    break;
                }
                    
                case ObjType::FuncRef: {
                    auto* fr = static_cast<ObjFuncRef*>(current);
                    if (fr->name) stack.push(fr->name);
                    break;
                }
                    
                case ObjType::ClassRef: {
                    auto* cr = static_cast<ObjClassRef*>(current);
                    if (cr->name) stack.push(cr->name);
                    break;
                }
                    
                case ObjType::FieldRef: {
                    auto* fr = static_cast<ObjFieldRef*>(current);
                    if (fr->className) stack.push(fr->className);
                    if (fr->fieldName) stack.push(fr->fieldName);
                    break;
                }
                    
                case ObjType::MethodRef:
                    break;
                    
                default:
                    break;
            }
        }
    }
    
    void sweep() {
        Obj* prev = nullptr;
        Obj* cur = objects_;
        lastFreedBytes = 0;
        lastFreedObjects = 0;
        
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
            
            lastFreedBytes += dead->size();
            lastFreedObjects++;
            destroyObject(dead);
        }
        
        allocatedBytes -= lastFreedBytes;
        totalFreedBytes += lastFreedBytes;
        totalFreedObjects += lastFreedObjects;
        
        if (lastFreedBytes > allocatedBytes / 2) {
            nextGcThreshold = allocatedBytes * 3 / 2;
        }
    }
    
    size_t getBytesAllocated() const { return allocatedBytes; }
    size_t getCollections() const { return collections; }
    size_t getTotalFreedBytes() const { return totalFreedBytes; }
    size_t getTotalFreedObjects() const { return totalFreedObjects; }
    
    size_t getObjectsCount() const {
        size_t count = 0;
        Obj* cur = objects_;
        while (cur) {
            count++;
            cur = cur->next;
        }
        return count;
    }
    
    void setLoggingEnabled(bool enabled) { enableLogging = enabled; }
    size_t getLastFreedBytes() const { return lastFreedBytes; }
    size_t getLastFreedObjects() const { return lastFreedObjects; }

private:
    Obj* objects_ = nullptr;
    size_t allocatedBytes = 0;
    size_t nextGcThreshold = 16 * 1024;
    size_t collections = 0;
    size_t totalFreedBytes = 0;
    size_t totalFreedObjects = 0;
    size_t lastFreedBytes = 0;
    size_t lastFreedObjects = 0;
    bool enableLogging = false;
    
    void markAll(std::function<void(Heap&)> markRoots) {
        markRoots(*this);
    }
    
    void checkGC() {
        if (allocatedBytes >= nextGcThreshold) {
            if (enableLogging) {
                std::cerr << "[GC WARNING] Memory threshold reached (" 
                          << allocatedBytes << " bytes), but no root provider.\n";
                std::cerr << "             GC will run on next instruction cycle.\n";
            }
        }
    }
    
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
        allocatedBytes = 0;
    }
};

}