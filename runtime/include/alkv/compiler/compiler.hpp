#pragma once
#include "alkv/bytecode/bytecode.hpp"
#include "alkv/bytecode/loader.hpp"
#include "alkv/vm/memory.hpp"
#include "asmjit/x86.h"
#include <iostream>
#include <map>
#include <string>
#include <unordered_map>

typedef void (*Func)();
typedef std::unordered_map<std::string, std::unordered_map<std::string, std::size_t>> g_fs_T;
typedef std::unordered_map<std::string, const alkv::bc::LoadedFunction*> fnByN_T;
const uint32_t CONST_HOT_PATH_TIMES = 100;

namespace alkv::compiler {
    class Compiler {
    public:
        Compiler(vm::VMMemory& m,
                          g_fs_T& g_fs,
                          fnByN_T& fnByN,
                          bool logging = false, bool error_handling = false);
        Func create_func(vm::Value*, uint8_t*, uint32_t size);
        vm::VMMemory& mem;
        g_fs_T& g_fieldSlots;
        fnByN_T& fnByName_;
    private:
        bool is_logging = false;
        bool is_error_handling = false;
        asmjit::JitRuntime rt;

        class MyErrorHandler : public asmjit::ErrorHandler {
        public:
            asmjit::Error err;

            MyErrorHandler() : err(asmjit::Error::kOk) {}

            void handle_error(asmjit::Error error, const char* msg, asmjit::BaseEmitter* origin) override {
                this->err = error;
                std::string message = std::string(msg);
                std::cerr << "ERROR: " << message;
            }
        };
    };
}
