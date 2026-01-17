#include "alkv/bytecode/loader.hpp"
#include <fstream>
#include <stdexcept>
#include <vector>
#include <cstring>
#include <algorithm>

namespace alkv::bc {

// --------------------
// Low-level BE readers
// --------------------
static void readExact(std::istream& in, void* dst, std::size_t n) {
    in.read(reinterpret_cast<char*>(dst), static_cast<std::streamsize>(n));
    if (in.gcount() != static_cast<std::streamsize>(n)) {
        throw std::runtime_error("ALKB loader: unexpected EOF");
    }
}

static uint8_t readU8(std::istream& in) {
    uint8_t v;
    readExact(in, &v, 1);
    return v;
}

static uint16_t readU16BE(std::istream& in) {
    uint8_t b[2];
    readExact(in, b, 2);
    return (uint16_t(b[0]) << 8) | uint16_t(b[1]);
}

static uint32_t readU32BE(std::istream& in) {
    uint8_t b[4];
    readExact(in, b, 4);
    return (uint32_t(b[0]) << 24) | (uint32_t(b[1]) << 16) | (uint32_t(b[2]) << 8) | uint32_t(b[3]);
}

static int32_t readI32BE(std::istream& in) {
    return static_cast<int32_t>(readU32BE(in));
}

static float readF32BE(std::istream& in) {
    uint32_t bits = readU32BE(in);
    float f;
    static_assert(sizeof(float) == sizeof(uint32_t));
    std::memcpy(&f, &bits, sizeof(float));
    return f;
}

static std::string readBytesAsString(std::istream& in, std::size_t n) {
    std::string s;
    s.resize(n);
    if (n != 0) readExact(in, s.data(), n);
    return s;
}

static void expectTag(std::istream& in, char a, char b) {
    char t[2];
    readExact(in, t, 2);
    if (t[0] != a || t[1] != b) {
        throw std::runtime_error(std::string("ALKB loader: expected tag '") + a + b + "'");
    }
}

static uint32_t inferRegCountFromCode(const std::vector<uint32_t>& code) {
    // Пытаемся вывести минимально достаточное кол-во регистров.
    // Берём max(A,B,C) по тем инструкциям, где поля имеют смысл.
    uint32_t maxReg = 0;
    bool any = false;

    for (uint32_t w : code) {
        auto op = decodeOp(w);
        auto abc = decodeABC(w);

        auto consider = [&](uint32_t r) {
            if (r == 255) return; // спец для RET void
            maxReg = std::max(maxReg, r);
            any = true;
        };

        switch (op) {
            case Opcode::LOADK: {
                auto abx = decodeABx(w);
                consider(abx.a);
                break;
            }
            case Opcode::MOV:
            case Opcode::ADD_I: case Opcode::SUB_I: case Opcode::MUL_I: case Opcode::DIV_I: case Opcode::MOD_I:
            case Opcode::ADD_F: case Opcode::SUB_F: case Opcode::MUL_F: case Opcode::DIV_F: case Opcode::MOD_F:
            case Opcode::LT_I: case Opcode::LE_I: case Opcode::GT_I: case Opcode::GE_I:
            case Opcode::LT_F: case Opcode::LE_F: case Opcode::GT_F: case Opcode::GE_F:
            case Opcode::EQ: case Opcode::NE:
            case Opcode::AND: case Opcode::OR:
            case Opcode::I2F:
            case Opcode::NOT:
            case Opcode::RET:
                consider(abc.a);
                consider(abc.b);
                consider(abc.c);
                break;

            case Opcode::JMP_T:
            case Opcode::JMP_F: {
                auto asbx = decodeAsBx(w);
                consider(asbx.a);
                break;
            }

            case Opcode::JMP:
            case Opcode::NOP:
                break;
        }
    }

    return any ? (maxReg + 1) : 0;
}

// --------------------
// Legacy: ALKB + CD only
// --------------------
Function loadLegacySingleCodeFile(const std::string& path) {
    std::ifstream in(path, std::ios::binary);
    if (!in) throw std::runtime_error("ALKB loader: cannot open file: " + path);

    char magic[4];
    readExact(in, magic, 4);
    if (!(magic[0] == 'A' && magic[1] == 'L' && magic[2] == 'K' && magic[3] == 'B')) {
        throw std::runtime_error("ALKB loader: bad magic (expected ALKB)");
    }

    uint16_t version = readU16BE(in);
    if (version != 1) throw std::runtime_error("ALKB loader: unsupported version");

    // Legacy writer пишет сразу CD
    expectTag(in, 'C', 'D');
    uint32_t sizeBytes = readU32BE(in);
    if (sizeBytes % 4 != 0) throw std::runtime_error("ALKB loader: CD size not multiple of 4");

    uint32_t nInsns = sizeBytes / 4;
    Function fn;
    fn.code.reserve(nInsns);

    for (uint32_t i = 0; i < nInsns; ++i) {
        uint32_t word = readU32BE(in); // DataOutputStream.writeInt -> BE
        fn.code.push_back(word);
    }

    fn.regCount = static_cast<uint16_t>(std::min<uint32_t>(inferRegCountFromCode(fn.code), 65535));
    // constPool пустой (в legacy формате CP не пишется)
    return fn;
}

// --------------------
// Module format: FN + functions
// --------------------
static LoadedFunction readOneFunction(std::istream& in, alkv::vm::Heap& heap) {
    LoadedFunction out;

    // FH
    expectTag(in, 'F', 'H');
    uint32_t fhSize = readU32BE(in);
    (void)fhSize; // размер нам не особо нужен, но он есть

    uint16_t nameLen = readU16BE(in);
    out.name = readBytesAsString(in, nameLen);
    out.numParams = readU32BE(in);
    uint32_t numRegs = readU32BE(in);
    if (numRegs > 65535) throw std::runtime_error("ALKB loader: regCount too large");
    out.fn.regCount = static_cast<uint16_t>(numRegs);

    // CP
    expectTag(in, 'C', 'P');
    uint32_t cpSize = readU32BE(in);
    (void)cpSize;

    uint32_t nConsts = readU32BE(in);
    out.fn.constPool.reserve(nConsts);

    for (uint32_t i = 0; i < nConsts; ++i) {
        uint8_t type = readU8(in);
        switch (type) {
            case 0: { // int
                int32_t v = readI32BE(in);
                out.fn.constPool.push_back(alkv::vm::Value::i32(v));
                break;
            }
            case 1: { // float
                float v = readF32BE(in);
                out.fn.constPool.push_back(alkv::vm::Value::f32(v));
                break;
            }
            case 2: { // bool (writeBoolean -> 1 byte)
                uint8_t b = readU8(in);
                out.fn.constPool.push_back(alkv::vm::Value::boolean(b != 0));
                break;
            }
            case 3: { // string
                uint32_t len = readU32BE(in);
                std::string s = readBytesAsString(in, len);
                auto* obj = heap.allocString(std::string_view(s.data(), s.size()));
                out.fn.constPool.push_back(alkv::vm::Value::object(obj));
                break;
            }
            default:
                throw std::runtime_error("ALKB loader: unknown const type in CP");
        }
    }

    // CD
    expectTag(in, 'C', 'D');
    uint32_t cdSize = readU32BE(in);
    if (cdSize % 4 != 0) throw std::runtime_error("ALKB loader: CD size not multiple of 4");

    uint32_t nInsns = cdSize / 4;
    out.fn.code.reserve(nInsns);
    for (uint32_t i = 0; i < nInsns; ++i) {
        uint32_t word = readU32BE(in);
        out.fn.code.push_back(word);
    }

    return out;
}

std::vector<LoadedFunction> loadModuleFromFile(const std::string& path, alkv::vm::Heap& heap) {
    std::ifstream in(path, std::ios::binary);
    if (!in) throw std::runtime_error("ALKB loader: cannot open file: " + path);

    char magic[4];
    readExact(in, magic, 4);
    if (!(magic[0] == 'A' && magic[1] == 'L' && magic[2] == 'K' && magic[3] == 'B')) {
        throw std::runtime_error("ALKB loader: bad magic (expected ALKB)");
    }

    uint16_t version = readU16BE(in);
    if (version != 1) throw std::runtime_error("ALKB loader: unsupported version");

    // Далее может быть либо FN (модуль), либо CD (legacy)
    char tag[2];
    readExact(in, tag, 2);

    if (tag[0] == 'C' && tag[1] == 'D') {
        // legacy: откатываемся на 2 байта назад не будем — проще переоткрыть и вызвать legacy loader
        // (либо можно хранить буфер, но так проще)
        in.close();
        // Вернём "модуль" из одной анонимной функции
        LoadedFunction lf;
        lf.name = "main";
        lf.numParams = 0;
        lf.fn = loadLegacySingleCodeFile(path);
        return { lf };
    }

    if (!(tag[0] == 'F' && tag[1] == 'N')) {
        throw std::runtime_error("ALKB loader: expected FN or CD section");
    }

    uint32_t numFunctions = readU32BE(in);
    std::vector<LoadedFunction> fns;
    fns.reserve(numFunctions);

    for (uint32_t i = 0; i < numFunctions; ++i) {
        fns.push_back(readOneFunction(in, heap));
    }

    return fns;
}

LoadedFunction loadFunctionByName(const std::string& path, alkv::vm::Heap& heap, const std::string& name) {
    auto all = loadModuleFromFile(path, heap);
    for (auto& f : all) {
        if (f.name == name) return f;
    }
    throw std::runtime_error("ALKB loader: function not found: " + name);
}

} // namespace alkv::bc