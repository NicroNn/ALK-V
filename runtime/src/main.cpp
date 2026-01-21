#include <iostream>
#include <string>
#include <cstring>
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
                std::cout << "<obj type=" << static_cast<int>(v.as.obj->type) << ">\n";
            }
            return;
    }
}

static void printUsage() {
    std::cout << "Usage: alkv_vm <file.alkb> [functionName] [options]\n";
    std::cout << "Options:\n";
    std::cout << "  --force-gc     Force garbage collection before execution\n";
    std::cout << "  --stats        Print GC statistics\n";
    std::cout << "  --help         Show this help\n";
}

int main(int argc, char** argv) {
    if (argc < 2) {
        printUsage();
        return 2;
    }
    
    std::string path;
    std::string fnName = "main";
    bool forceGC = false;
    bool showStats = false;
    
    // Парсинг аргументов
    for (int i = 1; i < argc; ++i) {
        if (strcmp(argv[i], "--help") == 0) {
            printUsage();
            return 0;
        } else if (strcmp(argv[i], "--force-gc") == 0) {
            forceGC = true;
        } else if (strcmp(argv[i], "--stats") == 0) {
            showStats = true;
        } else if (argv[i][0] != '-') {
            if (path.empty()) {
                path = argv[i];
            } else {
                fnName = argv[i];
            }
        }
    }
    
    if (path.empty()) {
        std::cerr << "Error: No input file specified\n";
        printUsage();
        return 2;
    }

    try {
        vm::VM machine;

        auto all = bc::loadModuleFromFile(path, machine.mem.heap);
        machine.loadModule(all);
        
        if (forceGC) {
            std::cout << "Forcing garbage collection...\n";
            machine.mem.forceGC();
        }

        vm::Value result = machine.run(fnName, {}, true);
        
        if (showStats) {
            machine.mem.updateStats();
            machine.mem.stats.print();
        }
        
        printValue(result);
        return 0;
    } catch (const std::exception& e) {
        std::cerr << "VM error: " << e.what() << "\n";
        return 1;
    }
}