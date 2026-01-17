#pragma once
#include <string>
#include <vector>
#include "alkv/bytecode/bytecode.hpp"
#include "alkv/vm/heap.hpp"

namespace alkv::bc {

struct LoadedFunction {
    std::string name;
    uint32_t numParams = 0;
    Function fn;
};

// Загрузить все функции из .alkb (модульный формат).
std::vector<LoadedFunction> loadModuleFromFile(const std::string& path, alkv::vm::Heap& heap);

// Удобный хелпер: загрузить функцию по имени (обычно main).
// Если не найдена — кидает исключение.
LoadedFunction loadFunctionByName(const std::string& path, alkv::vm::Heap& heap, const std::string& name = "main");

// Legacy: файл содержит только секцию CD (без FN/FH/CP). Редко нужно, но поддерживаем.
Function loadLegacySingleCodeFile(const std::string& path);

} // namespace alkv::bc