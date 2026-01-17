#include <iostream>
#include <string>
#include "alkv/vm.hpp"
#include "alkv/bytecode/loader.hpp"
#include "alkv/vm/object.hpp"

using namespace alkv;

static void printValue(const vm::Value& v) {
    switch (v.tag) {
        case vm::ValueTag::Nil:   std::cout << "nil\n"; return;
        case vm::ValueTag::Int:   std::cout << v.as.i << "\n"; return;
        case vm::ValueTag::Float: std::cout << v.as.f << "\n"; return;
        case vm::ValueTag::Bool:  std::cout << (v.as.b ? "true" : "false") << "\n"; return;
        case vm::ValueTag::Obj:
            if (v.as.obj && v.as.obj->type == vm::ObjType::String) {
                auto* s = static_cast<vm::ObjString*>(v.as.obj);
                std::cout << s->view() << "\n";
            } else {
                std::cout << "<obj>\n";
            }
            return;
    }
}

int main(int argc, char** argv) {
    if (argc < 2) {
        std::cerr << "Usage: alkv_vm <file.alkb> [functionName]\n";
        return 2;
    }

    std::string path = argv[1];
    std::string fnName = (argc >= 3) ? argv[2] : "main";

    try {
        vm::VM machine;

        // загружаем выбранную функцию; строки попадут в machine.mem.heap
        auto loaded = bc::loadFunctionByName(path, machine.mem.heap, fnName);

        // Сейчас CALL нет, поэтому args пустые; numParams можно проверять:
        if (loaded.numParams != 0) {
            std::cerr << "Warning: function expects params=" << loaded.numParams
                      << ", but VM demo runs with no args.\n";
        }

        vm::Value result = machine.run(loaded.fn, {});
        printValue(result);
        return 0;
    } catch (const std::exception& e) {
        std::cerr << "VM error: " << e.what() << "\n";
        return 1;
    }
}