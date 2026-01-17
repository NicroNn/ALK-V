#pragma once
#include <cstdint>
#include <vector>
#include "alkv/vm/value.hpp"

namespace alkv::bc {

enum class Opcode : uint8_t {
    NOP,
    MOV,
    LOADK,

    ADD_I, SUB_I, MUL_I, DIV_I, MOD_I,
    ADD_F, SUB_F, MUL_F, DIV_F, MOD_F,

    LT_I, LE_I, GT_I, GE_I,
    LT_F, LE_F, GT_F, GE_F,

    EQ, NE,
    NOT, AND, OR,

    JMP,
    JMP_T,
    JMP_F,

    RET,
    I2F
};

struct DecodedABC { Opcode op; uint8_t a,b,c; };
struct DecodedABx { Opcode op; uint8_t a; uint16_t bx; };
struct DecodedAsBx { Opcode op; uint8_t a; int16_t sbx; };

inline Opcode decodeOp(uint32_t w) { return static_cast<Opcode>(w & 0xFFu); }

inline DecodedABC decodeABC(uint32_t w) {
    return { decodeOp(w),
             static_cast<uint8_t>((w >> 8) & 0xFFu),
             static_cast<uint8_t>((w >> 16) & 0xFFu),
             static_cast<uint8_t>((w >> 24) & 0xFFu) };
}

inline DecodedABx decodeABx(uint32_t w) {
    return { decodeOp(w),
             static_cast<uint8_t>((w >> 8) & 0xFFu),
             static_cast<uint16_t>((w >> 16) & 0xFFFFu) };
}

inline DecodedAsBx decodeAsBx(uint32_t w) {
    uint16_t u = static_cast<uint16_t>((w >> 16) & 0xFFFFu);
    int16_t s = static_cast<int16_t>(u);
    return { decodeOp(w),
             static_cast<uint8_t>((w >> 8) & 0xFFu),
             s };
}

struct Function {
    std::vector<alkv::vm::Value> constPool; // runtime values
    std::vector<uint32_t> code;             // instruction words
    uint16_t regCount = 0;
};

} // namespace alkv::bc